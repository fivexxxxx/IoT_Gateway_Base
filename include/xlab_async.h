#ifndef XLAB_ASYNC_H
#define XLAB_ASYNC_H

#include "xlab_router.h"
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>

struct xlab_client_session;
/* 异步队列深度 (可通过 raifull.conf 扩展) */
#define XLAB_ASYNC_QUEUE_DEPTH 512

/* 异步任务结构 (全局静态预分配, 跨线程传递上下文) */
typedef struct {
    int             socket_fd;          /* 关联客户端 socket, 用于 flush 时定位 cs */
    uint32_t        session_id;
    uint32_t        seq;                /* 请求序列号, 保证响应顺序 */
    xlab_packte_cmd_e cmd_code;         /* 命令码, 响应构建必需 */
    uint8_t         params[XLAB_PROTO_PAYLOAD_MAX];
    uint8_t         param_len;
    xlab_cmd_handler_t handler;         /* 实际阻塞业务函数 */
    xlab_err_code_e result_code;        /* 后台线程执行结果 */
    uint8_t         resp_data[64];
    uint8_t         resp_len;
    void* user_data;
    uint8_t         valid;              /* 1=已提交/执行中, 0=空闲 */
    uint8_t         completed;          /* 1=后台执行完毕, 等待 Worker 回收 */
    /* 预缓存会话指针，避免 flush 时线性查找 */
    struct xlab_client_session* target_cs;
} xlab_async_task_t;

/* 初始化异步框架 (创建 eventfd 与后台硬件线程) */
int xlab_async_init(void);

/* 获取 eventfd (供 Worker 注册到 epoll 监听) */
int xlab_async_get_event_fd(void);

/* 提交阻塞任务 (Worker 线程调用, 非阻塞, 立即返回) */
int xlab_async_submit(
    int socket_fd,
    uint32_t session_id,
    uint32_t seq,
    xlab_packte_cmd_e cmd_code,
    const uint8_t* params,
    uint8_t p_len,
    xlab_cmd_handler_t handler,
    void* user_data);

/* Worker 线程处理 eventfd 通知, 按 seq 顺序构建响应并标记待发送 */
void xlab_async_flush_completed(struct xlab_client_session* cs);

#endif /* XLAB_ASYNC_H */