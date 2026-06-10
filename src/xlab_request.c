#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <time.h>
#include <netdb.h>
#include <sys/wait.h>
#include <signal.h>
#include <ctype.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <fcntl.h>
#include "xlab_request.h"
#include "xlab_string.h"
#include "xlab_config.h"
#include "xlab_scheduler.h"
#include "xlab_epoll.h"
#include "xlab_utils.h"
#include "xlab_memory.h"
#include "xlab_socket.h"
#include "xlab_macros.h"
#include "xlab_proto_parser.h"
#include "xlab_router.h"
#include "xlab_async.h"

pthread_key_t request_list_k;

int xlab_handler_read(int socket, struct xlab_client_session* cs)
{
    int bytes;

    /* 1. 读取数据到缓冲 */
    bytes = xlab_socket_read(socket, cs->body + cs->body_length, (cs->body_size - cs->body_length));
    if (bytes < 0) {
        if (errno == EAGAIN) return 1;
        xlab_session_remove(socket);
        return -1;
    }
    if (bytes == 0) {
        xlab_session_remove(socket);
        return -1;
    }

    /* 2. 逐字节解析 + 路由决策 */
    if (bytes > 0) {
        int i;
        xlab_packte_cmd_e cmd;
        uint8_t payload[XLAB_PROTO_PAYLOAD_MAX];
        uint8_t p_len;
        uint32_t seq;

        for (i = 0; i < bytes; i++) {
            xlab_parse_result_e res = xlab_proto_parse_byte(
                &cs->proto_ctx,
                cs->body[cs->body_length + i],
                &cmd, payload, &p_len, &seq
            );

            if (res == XLAB_PARSE_OK) {
                /* 查表获取命令属性 */
                xlab_router_entry_t* entry = xlab_router_lookup(cmd);
                /* 注: xlab_router_lookup 需在 xlab_router.c 中改为非 static 或暴露接口 */

                if (entry && (entry->flags & XLAB_CMD_FLAG_BLOCKING)) {
                    /* 阻塞型业务: 提交异步队列, 立即返回 */
                    if (xlab_async_submit(socket, cs->proto_ctx.next_req_seq - 1, seq,
                        cmd, payload, p_len, entry->handler, NULL) == 0) {
                        /* 提交成功: 状态保持 INCOMPLETE, 等待 eventfd 唤醒 */
                        /*xlab_info("[异步提交] cmd=0x%02X, seq=%u", cmd, seq);*/
                    }
                    else {
                        /* 队列满: 降级为同步错误或返回 BUSY */
                        uint8_t tmp_resp_len = 0;
                        uint8_t next_tail = (uint8_t)((cs->resp_q_tail + 1) % XLAB_RESP_QUEUE_DEPTH);
                        if (next_tail != cs->resp_q_head) {
                            xlab_router_build_response(NONE, XLAB_ERR_CHECKSUM, NULL, 0, seq,
                                cs->resp_queue[cs->resp_q_tail].buf, &tmp_resp_len);
                            cs->resp_queue[cs->resp_q_tail].len = tmp_resp_len;
                            cs->resp_queue[cs->resp_q_tail].sent_len = 0;

                            if (cs->resp_q_head == cs->resp_q_tail) {
                                int efd = (int)(intptr_t)pthread_getspecific(epoll_fd_k);
                                if (efd > 0) xlab_epoll_change_mode(efd, cs->socket, XLAB_EPOLL_WRITE, XLAB_EPOLL_LEVEL_TRIGGERED);
                            }
                            cs->resp_q_tail = next_tail;
                        }
                    }
                }
                else {
                    /* 非阻塞型业务：同步执行 (写入环形队列) */
                    uint8_t tmp_resp_len = 0;
                    uint8_t next_tail = (uint8_t)((cs->resp_q_tail + 1) % XLAB_RESP_QUEUE_DEPTH);

                    /* 检查队列是否已满 */
                    if (next_tail != cs->resp_q_head) {
                        int router_ret = xlab_router_process(cmd, payload, p_len, seq,
                            cs->resp_queue[cs->resp_q_tail].buf, &tmp_resp_len);
                        if (router_ret == 0) {
                            /* 写入队列槽位 */
                            cs->resp_queue[cs->resp_q_tail].len = tmp_resp_len;
                            cs->resp_queue[cs->resp_q_tail].sent_len = 0;

                            /* 若队列从空变非空，触发 EPOLLOUT */
                            if (cs->resp_q_head == cs->resp_q_tail) {
                                int efd = (int)(intptr_t)pthread_getspecific(epoll_fd_k);
                                if (efd > 0) {
                                    xlab_epoll_change_mode(efd, cs->socket,
                                        XLAB_EPOLL_WRITE, XLAB_EPOLL_LEVEL_TRIGGERED);
                                }
                            }

                            cs->resp_q_tail = next_tail;
                            cs->status = XLAB_REQUEST_STATUS_COMPLETED;  /* 保持兼容，实际由 EPOLLOUT 驱动 */
                        }
                    }
                    else {
                        xlab_warn("[Sync] Response queue full for non-blocking cmd=0x%02X", cmd);
                    }
                }
            }
            else if (res == XLAB_PARSE_CHK_ERR) {
                /* 校验和错误：构建 0xF4 响应 (写入环形队列) */
                uint8_t tmp_resp_len = 0;
                uint8_t next_tail = (uint8_t)((cs->resp_q_tail + 1) % XLAB_RESP_QUEUE_DEPTH);

                if (next_tail != cs->resp_q_head) {
                    xlab_router_build_response(NONE, XLAB_ERR_CHECKSUM, NULL, 0, seq,
                        cs->resp_queue[cs->resp_q_tail].buf, &tmp_resp_len);

                    cs->resp_queue[cs->resp_q_tail].len = tmp_resp_len;
                    cs->resp_queue[cs->resp_q_tail].sent_len = 0;

                    /* 若队列从空变非空，触发 EPOLLOUT */
                    if (cs->resp_q_head == cs->resp_q_tail) {
                        int efd = (int)(intptr_t)pthread_getspecific(epoll_fd_k);
                        if (efd > 0) {
                            xlab_epoll_change_mode(efd, cs->socket,
                                XLAB_EPOLL_WRITE, XLAB_EPOLL_LEVEL_TRIGGERED);
                        }
                    }

                    cs->resp_q_tail = next_tail;
                    cs->status = XLAB_REQUEST_STATUS_COMPLETED;
                }
                else {
                    xlab_warn("[Error] Response queue full for CHK_ERR");
                }
            }
            else if (res == XLAB_PARSE_ERROR) {
                xlab_err("[解析致命错误] 跳过非法字节");
            }
        }
        cs->body_length += bytes;
    }
    return bytes;
}

int xlab_handler_write(int socket, struct xlab_client_session* cs)
{
    struct sched_list_node* sched = xlab_sched_get_thread_conf();
    int sent_any = 0;

    /* 循环消费队列，每次最多聚合 3 个完整槽位批量发送，减少 syscall 开销 */
    while (cs->resp_q_head != cs->resp_q_tail) {
        struct iovec iov[3];
        int iov_cnt = 0;
        uint8_t scan_head = cs->resp_q_head;

        /* 预取连续且未发送的槽位 */
        for (int i = 0; i < 3 && scan_head != cs->resp_q_tail; i++) {
            resp_slot_t* slot = &cs->resp_queue[scan_head];
            if (slot->sent_len == 0) {
                iov[iov_cnt].iov_base = slot->buf;
                iov[iov_cnt].iov_len = slot->len;
                iov_cnt++;
                scan_head = (uint8_t)((scan_head + 1) % XLAB_RESP_QUEUE_DEPTH);
            }
            else {
                break; /* 遇到历史部分发送槽位，停止聚合 */
            }
        }

        if (iov_cnt == 0) break;

        ssize_t total_sent = writev(socket, iov, iov_cnt);
        if (total_sent > 0) {
            sent_any = 1;
            int sent = (int)total_sent;
            uint8_t cur = cs->resp_q_head;

            /* 精准推进队列头指针，处理跨槽位发送 */
            while (sent > 0 && cur != cs->resp_q_tail) {
                resp_slot_t* slot = &cs->resp_queue[cur];
                uint16_t remaining = (uint16_t)(slot->len - slot->sent_len);
                if (sent >= remaining) {
                    sent -= remaining;
                    slot->sent_len = slot->len; /* 标记完整发送 */
                    cs->resp_q_head = (uint8_t)((cur + 1) % XLAB_RESP_QUEUE_DEPTH);
                    cur = cs->resp_q_head;
                }
                else {
                    //slot->sent_len +=(uint16_t)sent;
                    slot->sent_len = slot->sent_len+(uint16_t)sent;
                    sent = 0;
                    break; /* 当前槽位未发完，保留状态等待下次 EPOLLOUT */
                }
            }
        }
        else if (total_sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            xlab_err("Socket %d write fatal error, closing", socket);
            xlab_session_remove(socket);
            return -1;
        }
        else {
            break; /* EAGAIN: 内核缓冲满，跳出等待 */
        }
    }

    /* 队列清空：切回 EPOLLIN；否则保持 EPOLLOUT */
    if (cs->resp_q_head == cs->resp_q_tail) {
        xlab_epoll_change_mode(sched->epoll_fd, socket, XLAB_EPOLL_READ, XLAB_EPOLL_LEVEL_TRIGGERED);
    }

    return sent_any ? 0 : -1;
}

struct xlab_client_session *xlab_session_create(int socket, struct sched_list_node *sched)
{
    struct xlab_client_session *cs;
    struct sched_connection *sc;
    struct xlab_list *cs_list;

    sc = xlab_sched_get_connection(sched, socket);
    if (!sc) {
        return NULL;
    }

    /* 为节点分配内存 */
    cs = xlab_mem_malloc(sizeof(struct xlab_client_session));

    //cs->pipelined = XLAB_FALSE;
    cs->socket = socket;
    cs->status = XLAB_REQUEST_STATUS_INCOMPLETE;

    /* 创建时间(Unix时间) */
    cs->init_time = sc->arrive_time;

    /* 为 body 内容分配空间 */
    cs->body = cs->body_fixed;

    /* 基于 Chunk 字节的缓冲区大小 */
    cs->body_size = XLAB_REQUEST_CHUNK;
    /* 当前数据长度 */
    cs->body_length = 0;

    cs->body_pos_end = -1;
    /* 核心修复：显式初始化新增字段 (因 xlab_mem_malloc 不清零) */
    /* 1. 初始化协议解析上下文 */
    memset(&cs->proto_ctx, 0, sizeof(xlab_proto_ctx_t));
    cs->proto_ctx.state = XLAB_PARSE_WAIT_HEAD;

    /* 2. 初始化环形响应队列指针 (防随机值导致队列逻辑崩溃) */
    cs->resp_q_head = 0;
    cs->resp_q_tail = 0;

    /* 3. 初始化序列号计数器 */
    cs->resp_seq_expected = 0;
    /* 初始化会话请求列表 */

    /* 将此 SESSION 添加到线程列表 */
    cs_list = xlab_sched_get_request_list();

    /* 将节点添加到列表 */
    xlab_list_add(&cs->_head, cs_list);

    /* 再次设置全局列表 */
    xlab_sched_set_request_list(cs_list);

    return cs;
}

struct xlab_client_session *xlab_session_get(int socket)
{
    struct xlab_client_session *cs_node = NULL;
    struct xlab_list *cs_list, *cs_head;

    cs_list = xlab_sched_get_request_list();
    xlab_list_foreach(cs_head, cs_list) {
        cs_node = xlab_list_entry(cs_head, struct xlab_client_session, _head);
        if (cs_node->socket == socket) {
            return cs_node;
        }
    }

    return NULL;
}

void xlab_session_remove(int socket)
{
    struct xlab_client_session *cs_node;
    struct xlab_list *cs_list, *cs_head, *temp;

    cs_list = xlab_sched_get_request_list();

    xlab_list_foreach_safe(cs_head, temp, cs_list) {
        cs_node = xlab_list_entry(cs_head, struct xlab_client_session, _head);
        if (cs_node->socket == socket) {
            xlab_list_del(cs_head);
            /* 清理传输层映射表 (防止 fd 复用混淆,没卵用呀) */
            {
                /* 外部声明，避免修改头文件依赖 */
                extern void trans_fd_map_remove(int fd);
                trans_fd_map_remove(socket);
            }
            if (cs_node->body != cs_node->body_fixed) {
                xlab_mem_free(cs_node->body);
            }
            xlab_mem_free(cs_node);
            break;
        }
    }

    /* 更新线程索引 */
    xlab_sched_set_request_list(cs_list);
}