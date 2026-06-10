#ifndef XLAB_REQUEST_H
#define XLAB_REQUEST_H

#include "xlab_memory.h"
#include "xlab_scheduler.h"
#include "xlab_limits.h"
#include "xlab_proto_parser.h"  /* 必须在此处引入 */

#define XLAB_REQUEST_CHUNK (int) 4096
#define XLAB_REQUEST_STATUS_INCOMPLETE -1
#define XLAB_REQUEST_STATUS_COMPLETED 0
#define XLAB_RESP_QUEUE_DEPTH 64  /* 环形队列深度，可根据内存调整 */

extern pthread_key_t request_list_k;
typedef struct {
    uint8_t buf[XLAB_RESP_BUF_SIZE];  /* 单条响应缓冲 */
    uint16_t len;                      /* 有效数据长度 */
    uint16_t sent_len;                 /* 已发送字节数 */
} resp_slot_t;

struct xlab_client_session {
    int socket;
    int status;
    unsigned char* body;
    unsigned char body_fixed[XLAB_REQUEST_CHUNK];
    int body_size;
    int body_length;
    int body_pos_end;
    time_t init_time;
    struct xlab_list _head;    
    xlab_proto_ctx_t proto_ctx;/* 新增字段 字节流状态机上下文*/
    resp_slot_t resp_queue[XLAB_RESP_QUEUE_DEPTH];  /* 64槽位响应队列 */
    uint8_t resp_q_head;  /* 队列头：下一个待发送的槽位索引 */
    uint8_t resp_q_tail;  /* 队列尾：下一个可写入的槽位索引 */
    uint32_t resp_seq_expected;  /* 顺序保证核心：期望发送的序列号 */
}__attribute__((aligned(64)));/* 缓存行对齐，避免多线程 epoll 访问引发 False Sharing */
/* 编译期拦截结构体膨胀，保障内存布局符合预期 */
_Static_assert(sizeof(resp_slot_t) <= 260, "resp_slot_t size exceeds cache line limit");

struct xlab_client_session* xlab_session_create(int socket, struct sched_list_node* sched);
struct xlab_client_session* xlab_session_get(int socket);
void xlab_session_remove(int socket);
int xlab_handler_read(int socket, struct xlab_client_session* cs);
int xlab_handler_write(int socket, struct xlab_client_session* cs);

#endif /* XLAB_REQUEST_H */