#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "xlab_macros.h"
#include "xlab_scheduler.h"
#include "xlab_epoll.h"
#include "xlab_request.h"
#include "xlab_socket.h"
#include "xlab_utils.h"
#include "xlab_config.h"
#include "xlab_trans.h"          /* 传输抽象层头文件 */
#include "xlab_proto_parser.h"   /* 协议解析头文件 */
#include "xlab_router.h"
#include "xlab_async.h" 
#include "xlab_trans.h"


int xlab_conn_read(int socket)
{
    struct sched_list_node* sched = xlab_sched_get_thread_conf();

    /* 拦截 eventfd 通知事件 */
    int async_efd = xlab_async_get_event_fd();
    if (socket == async_efd) {
        /* 读取 eventfd 清空状态 */
        uint64_t val;
        if (read(socket, &val, sizeof(val)) != sizeof(val)) {
            xlab_warn("eventfd read failed: %s", strerror(errno));
        }

        /* 处理所有已完成的异步任务 */
        /* 注意：这里传入 NULL，让 flush 函数遍历所有活跃会话查找匹配任务 */
        xlab_async_flush_completed(NULL);
        return 0;
    }

    /* UDP/UART 处理逻辑 (无状态 pos 循环解析，对齐 TCP) */
    int udp_fd = xlab_trans_get_fd(XLAB_TRANS_UDP);
    int uart_fd = xlab_trans_get_fd(XLAB_TRANS_UART);

    if (socket == udp_fd || socket == uart_fd) {
        uint8_t tmp_buf[256];
        int n = xlab_trans_read_dispatch(socket, tmp_buf, sizeof(tmp_buf));

        if (n > 0) {
            int pos = 0;
            uint32_t udp_seq = 0; /* 单数据报内独立序列号 */

            /* 核心：逐字节扫描 + 边界对齐 + 逐帧处理 (无跨包状态残留) */
            while (pos < n) {
                /* 1. 扫描引导码 0x40 */
                if (tmp_buf[pos] != XLAB_BOOT_CODE_HEAD) {
                    pos++; continue;
                }

                /* 2. 长度字段合法性 */
                if (pos + 1 >= n) break; /* 剩余数据不足读长度，直接丢弃 */
                uint8_t len_field = tmp_buf[pos + 1];
                if (len_field < 2 || len_field > XLAB_PROTO_PAYLOAD_MAX) {
                    pos++; continue; /* 非法长度，跳过当前头 */
                }

                /* 3. 帧完整性检查 (UDP 无续传机制，不完整直接报错丢弃) */
                int frame_len = 2 + len_field;
                if (pos + frame_len > n) {
                    uint8_t err_buf[8], err_len = 0;
                    xlab_router_build_response(NONE, XLAB_ERR_CHECKSUM, NULL, 0, udp_seq++, err_buf, &err_len);
                    xlab_trans_write_dispatch(socket, err_buf, err_len);
                    xlab_warn("[UDP] Incomplete frame at pos %d dropped", pos);
                    pos++; /* 逐字节重同步 */
                    continue;
                }

                /* 4. 校验和验证 */
                uint8_t calc_chk = xlab_proto_calc_checksum(tmp_buf + pos, (uint8_t)frame_len);
                if (calc_chk == tmp_buf[pos + frame_len - 1]) {
                    /* 校验成功：提取命令并响应 */
                    xlab_packte_cmd_e cmd = tmp_buf[pos + 2];
                    uint8_t* payload = tmp_buf + pos + 3;
                    uint8_t p_len = (uint8_t)(len_field - 2);

                    uint8_t resp_buf[XLAB_RESP_BUF_SIZE], resp_len = 0;
                    int router_ret = xlab_router_process(cmd, payload, p_len, udp_seq++, resp_buf, &resp_len);
                    if (router_ret == 0) {
                        xlab_trans_write_dispatch(socket, resp_buf, resp_len);
                    }
                    pos += frame_len; /* 跳过完整帧 */
                }
                else {
                    /* 校验失败：立即返回 F4，逐字节重同步 */
                    uint8_t err_buf[8], err_len = 0;
                    xlab_router_build_response(NONE, XLAB_ERR_CHECKSUM, NULL, 0, udp_seq++, err_buf, &err_len);
                    xlab_trans_write_dispatch(socket, err_buf, err_len);
                    xlab_warn("[UDP] Checksum fail at pos %d, resyncing...", pos);
                    pos++; /* 仅跳过 0x40，防吞下一帧头 */
                }
            }
        }
        else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            xlab_warn("UDP/UART read error fd=%d: %s", socket, strerror(errno));
        }
        return n;
    }

    /* TCP 处理逻辑 */
    struct xlab_client_session* cs = xlab_session_get(socket);
    if (!cs) {
        if (xlab_socket_set_tcp_nodelay(socket) != 0) {
            xlab_warn("TCP_NODELAY failed");
        }
        cs = xlab_session_create(socket, sched);
        if (!cs) return -1;
    }

    /* 读取 TCP 数据 */
    int ret = xlab_socket_read(socket, cs->body + cs->body_length, (cs->body_size - cs->body_length));
    if (ret > 0) {
        cs->body_length += ret;
        int pos = 0;

        /* 帧解析循环 */
        while (pos < cs->body_length) {
            /* 1. 逐字节扫描引导码 0x40 */
            if (cs->body[pos] != XLAB_BOOT_CODE_HEAD) {
                pos++;
                continue;
            }

            /* 2. 长度字段合法性检查 */
            if (pos + 1 >= cs->body_length) break;
            uint8_t len_field = cs->body[pos + 1];
            if (len_field < 2 || len_field > XLAB_PROTO_PAYLOAD_MAX) {
                pos++;
                continue;
            }

            /* 3. 帧长度计算 */
            int frame_len = 2 + len_field;

            /* 不完整/畸形帧立即报错丢弃，绝不阻塞后续合法帧 */
            if (pos + frame_len > cs->body_length) {
                uint32_t err_seq = cs->proto_ctx.next_req_seq++;
                uint8_t err_buf[8], err_len = 0;
                /* 长度不匹配视为协议错误，统一返回 F4 */
                xlab_router_build_response(NONE, XLAB_ERR_CHECKSUM, NULL, 0, err_seq, err_buf, &err_len);
                send(cs->socket, err_buf, err_len, MSG_NOSIGNAL);
                xlab_warn("[INCOMPLETE] Frame at pos %d dropped (len mismatch), sending F4", pos);
                pos++; /* 逐字节重同步，防吞头 */
                continue;
            }

            /* 4. 校验和验证 */
            uint8_t calc_chk = xlab_proto_calc_checksum(cs->body + pos, (uint8_t)frame_len);
            if (calc_chk == cs->body[pos + frame_len - 1]) {
                /* 校验成功：提取命令与负载 */
                xlab_packte_cmd_e cmd = cs->body[pos + 2];
                uint8_t* payload = cs->body + pos + 3;
                uint8_t p_len = (uint8_t)(len_field - 2);
                uint32_t seq = cs->proto_ctx.next_req_seq++;

                /* 路由决策 (复用直发优先+异步逻辑) */
                xlab_router_entry_t* entry = xlab_router_lookup(cmd);
                int handled = 0;

                /* 阻塞型：提交异步 */
                if (entry && (entry->flags & XLAB_CMD_FLAG_BLOCKING)) {
                    if (xlab_async_submit(socket, 0, seq, cmd, payload, p_len, entry->handler, NULL) == 0) {
                        handled = 1;
                    }
                }

                /* 非阻塞/提交失败：直发优先 */
                if (!handled) {
                    uint8_t tmp_buf[XLAB_RESP_BUF_SIZE], tmp_len = 0;
                    int router_ret = xlab_router_process(cmd, payload, p_len, seq, tmp_buf, &tmp_len);
                    if (router_ret == 0) {
                        ssize_t sent = send(cs->socket, tmp_buf, tmp_len, MSG_NOSIGNAL);
                        if (sent == tmp_len) handled = 1;
                        else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            uint8_t next_tail = (uint8_t)(cs->resp_q_tail + 1) % XLAB_RESP_QUEUE_DEPTH;
                            if (next_tail != cs->resp_q_head) {
                                memcpy(cs->resp_queue[cs->resp_q_tail].buf, tmp_buf, tmp_len);
                                cs->resp_queue[cs->resp_q_tail].len = tmp_len;
                                cs->resp_queue[cs->resp_q_tail].sent_len = 0;
                                cs->resp_q_tail = next_tail; handled = 1;
                                int efd = (int)(intptr_t)pthread_getspecific(epoll_fd_k);
                                if (efd > 0) xlab_epoll_change_mode(efd, cs->socket, XLAB_EPOLL_WRITE, XLAB_EPOLL_LEVEL_TRIGGERED);
                            }
                        }
                        /* 同步命令响应成功后，推进期望序列号，保证与异步Flush序列对齐 */
                        cs->resp_seq_expected = seq + 1;
                    }
                }

                /* 未知命令/错误：直发 */
                if (!handled) {
                    uint8_t tmp_buf[XLAB_RESP_BUF_SIZE], tmp_len = 0;
                    xlab_router_build_response(cmd, entry ? XLAB_ERR_INTERNAL : XLAB_ERR_UNKNOWN_CMD, NULL, 0, seq, tmp_buf, &tmp_len);
                    send(cs->socket, tmp_buf, tmp_len, MSG_NOSIGNAL);
                }

                pos += frame_len; /* 成功处理：跳过整帧 */
            }
            else {
                /* 校验失败：立即返回 F4，逐字节重同步 */
                uint32_t err_seq = cs->proto_ctx.next_req_seq++;
                uint8_t err_buf[8], err_len = 0;
                xlab_router_build_response(NONE, XLAB_ERR_CHECKSUM, NULL, 0, err_seq, err_buf, &err_len);
                send(cs->socket, err_buf, err_len, MSG_NOSIGNAL);
                xlab_warn("[CHK_ERR] Frame at pos %d dropped, resyncing byte-by-byte...", pos);
                pos++; /* 仅跳过引导码，防吞掉下一帧头 */
            }
        }

        /* 清理已解析数据，保留剩余未完成帧 */
        if (pos > 0) {
            memmove(cs->body, cs->body + pos, (uint8_t)(cs->body_length - pos));
            cs->body_length -= pos;
        }

        /* 缓冲区溢出保护 */
        if (cs->body_length >= config->max_request_size) {
            xlab_session_remove(socket);
            return -1;
        }
    }
    else if (ret == 0) {
        xlab_session_remove(socket);
        return -1;
    }
    else {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            xlab_session_remove(socket);
            return -1;
        }
    }

    return ret;
}

int xlab_conn_write(int socket)
{
    int ret = -1;
    struct xlab_client_session *cs;
    struct sched_list_node *sched;
    struct sched_connection *conx;

    sched = xlab_sched_get_thread_conf();

    /* 判断是否是新连接 */
    conx = xlab_sched_get_connection(sched, socket);
    if (!conx) {

#if 1
        if (xlab_sched_register_client(socket, sched) == -1) {
            return -1;
        }
#endif

        xlab_epoll_change_mode(sched->epoll_fd, socket,
                             XLAB_EPOLL_READ, XLAB_EPOLL_LEVEL_TRIGGERED);
        return 0;
    }

    xlab_sched_update_conn_status(sched, socket, XLAB_SCHEDULER_CONN_PROCESS);

    /* 从调度列表节点中获取包含当前客户端/socket 信息的节点 */
    cs = xlab_session_get(socket);
    if (!cs) {
        return -1;
    }

    ret = xlab_handler_write(socket, cs);

    /* 如果 ret < 0，表示写入调用时发生错误
     * 另一方面，0 表示已成功处理请求
     * 如果 ret > 0 表示仍需要发送数据
     */
    if (ret < 0) {        
        xlab_session_remove(socket);
        return -1;
    }
    else if (ret == 0) {
        /* 1. 重置会话状态：body 指针/长度/协议上下文，准备接收下一帧 */
        cs->body_pos_end = -1;
        cs->body_length = 0;
        cs->status = XLAB_REQUEST_STATUS_INCOMPLETE;

        /* 2. 切回读模式，等待下一帧 */
        xlab_epoll_change_mode(sched->epoll_fd, socket,
            XLAB_EPOLL_READ, XLAB_EPOLL_LEVEL_TRIGGERED);
        return 0;
    }
    else if (ret > 0) {
        return 0;
    }

    /* 避免 gcc 报错 */
    return -1;
}

int xlab_conn_error(int socket)
{
    struct xlab_client_session *cs;
    struct sched_list_node *sched;

    sched = xlab_sched_get_thread_conf();
    xlab_sched_remove_client(sched, socket);
    cs = xlab_session_get(socket);
    if (cs) {
        xlab_session_remove(socket);
    }

    return 0;
}

int xlab_conn_close(int socket)
{
    struct sched_list_node *sched;

    sched = xlab_sched_get_thread_conf();
    xlab_sched_remove_client(sched, socket);
    return 0;
}