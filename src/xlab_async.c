#include "xlab_macros.h"
#include "xlab_async.h"
#include "xlab_request.h"
#include "xlab_router.h"
#include "xlab_epoll.h"
#include <sys/eventfd.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

/* 全局异步上下文 */
static xlab_async_task_t g_async_queue[XLAB_ASYNC_QUEUE_DEPTH];
static pthread_mutex_t g_async_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_async_cond = PTHREAD_COND_INITIALIZER;
static int g_event_fd = -1;
static pthread_t g_async_thread;

/* 后台硬件线程: 专职执行阻塞型 Handler */
static void* xlab_async_worker_loop(void* arg)
{
    while (1) {
        pthread_mutex_lock(&g_async_mutex);

        /* 等待有效任务 */
        while (1) {
            int has_task = 0;
            for (int i = 0; i < XLAB_ASYNC_QUEUE_DEPTH; i++) {
                if (g_async_queue[i].valid && !g_async_queue[i].completed) {
                    has_task = 1;
                    break;
                }
            }
            if (has_task) break;
            pthread_cond_wait(&g_async_cond, &g_async_mutex);
        }

        /* 执行所有就绪任务 */
        for (int i = 0; i < XLAB_ASYNC_QUEUE_DEPTH; i++) {
            if (g_async_queue[i].valid && !g_async_queue[i].completed) {
                uint8_t tmp_resp[64] = { 0 };
                uint8_t tmp_len = 0;

                /* 调用实际业务 Handler (可能阻塞 UART/SPI/Flash) */
                int ret = g_async_queue[i].handler(
                    g_async_queue[i].params,
                    g_async_queue[i].param_len,
                    tmp_resp,
                    &tmp_len
                );

                /* 映射错误码 */
                g_async_queue[i].result_code = (ret < 0) ? (xlab_err_code_e)(-ret) : XLAB_ERR_OK;
                if (tmp_len > 0) {
                    memcpy(g_async_queue[i].resp_data, tmp_resp, tmp_len);
                }
                g_async_queue[i].resp_len = tmp_len;
                g_async_queue[i].completed = 1;
            }
        }
        pthread_mutex_unlock(&g_async_mutex);

        /* 唤醒 Worker 线程: eventfd 计数 +1 */
        uint64_t val = 1;
        ssize_t s = write(g_event_fd, &val, sizeof(val));
        if (s < 0) {
            /* 忽略 EAGAIN (满) 和 EINTR (信号中断) */
            if (errno != EAGAIN && errno != EINTR) {
                xlab_err("eventfd write failed, errno=%d (%s)", errno, strerror(errno));
            }
        }
    }
    return NULL;
}

int xlab_async_init(void)
{
    g_event_fd = eventfd(0, EFD_NONBLOCK);
    if (g_event_fd < 0) {
        xlab_err("Failed to create async eventfd");
        return -1;
    }

    memset(g_async_queue, 0, sizeof(g_async_queue));
    pthread_create(&g_async_thread, NULL, xlab_async_worker_loop, NULL);
    return 0;
}

int xlab_async_get_event_fd(void)
{
    return g_event_fd;
}

int xlab_async_submit(
    int socket_fd, uint32_t session_id, uint32_t seq,
    xlab_packte_cmd_e cmd_code,
    const uint8_t* params, uint8_t p_len,
    xlab_cmd_handler_t handler, void* user_data)
{
    int idx = -1;
    pthread_mutex_lock(&g_async_mutex);

    for (int i = 0; i < XLAB_ASYNC_QUEUE_DEPTH; i++) {
        if (!g_async_queue[i].valid) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        pthread_mutex_unlock(&g_async_mutex);
        return -1; /* 队列满 */
    }

    /* 填充任务上下文 */
    g_async_queue[idx].socket_fd = socket_fd;
    g_async_queue[idx].session_id = session_id;
    g_async_queue[idx].seq = seq;
    g_async_queue[idx].cmd_code = cmd_code;
    g_async_queue[idx].param_len = p_len;
    memcpy(g_async_queue[idx].params, params, p_len);
    g_async_queue[idx].handler = handler;
    g_async_queue[idx].user_data = user_data;
    g_async_queue[idx].valid = 1;
    g_async_queue[idx].completed = 0;
    /* Worker 线程本地直查，耗时 O(1) */
    g_async_queue[idx].target_cs = xlab_session_get(socket_fd);
    pthread_cond_signal(&g_async_cond);
    pthread_mutex_unlock(&g_async_mutex);
    return 0;
}

void xlab_async_flush_completed(struct xlab_client_session* target_cs)
{
    uint64_t val;
    while (read(g_event_fd, &val, sizeof(val)) > 0); /* 清空 eventfd */

    pthread_mutex_lock(&g_async_mutex);
    for (int i = 0; i < XLAB_ASYNC_QUEUE_DEPTH; i++) {
        xlab_async_task_t* t = &g_async_queue[i];
        if (!t->valid || !t->completed) continue;

        /*struct xlab_client_session* cs = target_cs ? target_cs : xlab_session_get(t->socket_fd);
        if (!cs) { t->valid = 0; t->completed = 0; continue; }*/

        /* 优先使用缓存指针，降级防御处理悬垂指针 */
        struct xlab_client_session* cs = t->target_cs;
        if (!cs || cs->socket != t->socket_fd) {
            cs = xlab_session_get(t->socket_fd); /* 仅在会话异常回收时触发，概率极低 */
        }
        if (!cs) {
            t->valid = 0;
            t->completed = 0;
            continue;
        }

        /* 顺序校验：非期望序列号直接跳过，留在队列等待 */
        if (t->seq != cs->resp_seq_expected) continue;

        /*uint8_t next_tail = (cs->resp_q_tail + 1) % XLAB_RESP_QUEUE_DEPTH;
        if (next_tail == cs->resp_q_head) {
            xlab_warn("[Async] Response queue full, dropping task seq=%u", t->seq);
            t->valid = 0; t->completed = 0;
            continue;
        }*/

        uint8_t next_tail = (uint8_t)(cs->resp_q_tail + 1) % XLAB_RESP_QUEUE_DEPTH;
        if (next_tail == cs->resp_q_head) {
            /* 队列满时，不直接丢弃，而是发送 BUSY 响应并推进序列号 */
            uint8_t err_buf[XLAB_RESP_BUF_SIZE];
            uint8_t err_len = 0;
            xlab_router_build_response(t->cmd_code, XLAB_ERR_BUS_BUSY, NULL, 0, t->seq, err_buf, &err_len);

            /* 尝试直接发送BUSY响应 */
            send(cs->socket, err_buf, err_len, MSG_NOSIGNAL);

            /* 关键: 推进序列号，打破死锁，让后续任务继续流转 */
            cs->resp_seq_expected = t->seq + 1;

            xlab_warn("[Async] Queue full, dropped seq=%u & sent BUSY", t->seq);
            t->valid = 0; t->completed = 0;
            continue;
        }

        /* 1. 写入新槽位 */
        uint8_t resp_len = 0;
        xlab_router_build_response(t->cmd_code, t->result_code,
            t->resp_data, t->resp_len, t->seq,
            cs->resp_queue[cs->resp_q_tail].buf, &resp_len);
        cs->resp_queue[cs->resp_q_tail].len = resp_len;
        cs->resp_queue[cs->resp_q_tail].sent_len = 0;

        /* 2. 推进队列尾指针 & 清理任务 */
        cs->resp_q_tail = next_tail;
        cs->resp_seq_expected++;
        t->valid = 0;
        t->completed = 0;

        /* 3. 仅当队列从"空"变"非空"时，才注册 EPOLLOUT */
        if (cs->resp_q_head != cs->resp_q_tail) {
            int efd = (int)(size_t)pthread_getspecific(epoll_fd_k);
            if (efd > 0) {
                /* epoll_ctl(MOD) 是幂等的，重复调用开销极小 */
                xlab_epoll_change_mode(efd, cs->socket,
                    XLAB_EPOLL_WRITE, XLAB_EPOLL_LEVEL_TRIGGERED);
            }
        }
    }
    pthread_mutex_unlock(&g_async_mutex);
}