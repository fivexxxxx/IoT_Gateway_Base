#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <string.h>

#include "xlab_connection.h"
#include "xlab_scheduler.h"
#include "xlab_memory.h"
#include "xlab_epoll.h"
#include "xlab_request.h"
#include "xlab_config.h"
#include "xlab_utils.h"
#include "xlab_macros.h"
#include "xlab_trans.h"
#include "xlab_async.h"

static pthread_mutex_t mutex_sched_init = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_worker_init = PTHREAD_MUTEX_INITIALIZER;

/* 返回应处理新连接的 worker id，返回活动连接数最少的 worker id */
static inline int _next_target()
{
    int i;
    int target = 0;
    unsigned long long tmp = 0, cur = 0;

    cur = sched_list[0].accepted_connections - sched_list[0].closed_connections;
    if (cur == 0)
        return 0;

    /* 查找负载最低的 worker */
    for (i = 1; i < config->workers; i++) {
        tmp = sched_list[i].accepted_connections - sched_list[i].closed_connections;
        if (tmp < cur) {
            target = i;
            cur = tmp;

            if (cur == 0)
                break;
        }
    }

    /* 如果 sched_list[target] worker 已满，则整个服务器也已满，因为它负载最低。 */
    if(cur >= (size_t)config->worker_capacity) {
        return -1;
    }

    return target;
}

/* 将新连接分配给指定的 worker 线程，此调用来自主进程 */
int xlab_sched_add_client(int remote_fd)
{
    int r, t=0;
    struct sched_list_node *sched;

    /* 下一个 worker 目标 */
    t = _next_target();

    if (t == -1) {
        return -1;
    }

    sched = &sched_list[t];
    r  = xlab_epoll_add(sched->epoll_fd, remote_fd, XLAB_EPOLL_WRITE,
                      XLAB_EPOLL_LEVEL_TRIGGERED);

    /* 如果 epoll 失败，递减活动连接计数器 */
    if (r == 0) {
        sched->accepted_connections++;
    }

    return r;
}

/* 在调度器中注册新客户端连接，此调用发生在 worker/线程上下文中 */
int xlab_sched_register_client(int remote_fd, struct sched_list_node *sched)
{
    /*int ret;*/
    struct sched_connection *sched_conn;
    struct xlab_list *av_queue = &sched->av_queue;
    /* 获得结构体的首地址 */
    sched_conn = xlab_list_entry_first(av_queue, struct sched_connection, _head);

    xlab_list_del(&sched_conn->_head);
    xlab_list_add(&sched_conn->_head, &sched->busy_queue);

    /* Socket 和状态 */
    sched_conn->socket = remote_fd;
    sched_conn->status = XLAB_SCHEDULER_CONN_PENDING;


    return 0;
}

static void xlab_sched_thread_lists_init()
{
    struct xlab_list *cs_list;

    /* xlab_client_session xlab_list */
    cs_list = xlab_mem_malloc(sizeof(struct xlab_list));
    xlab_list_init(cs_list);
    xlab_sched_set_request_list(cs_list);
}

/* 创建线程，所有调用都在线程上下文中 */
static void *xlab_sched_launch_worker_loop(void *thread_conf)
{
    sched_thread_conf *thconf = thread_conf;
    int wid, epoll_max_events = thconf->epoll_max_events;
    struct sched_list_node *thinfo = NULL;
    xlab_epoll_handlers *handler;


    /* 初始化特定线程缓存 */
    xlab_sched_thread_lists_init();

    /* 注册工作线程 */
    wid = xlab_sched_register_thread(thconf->epoll_fd);
    thinfo = &sched_list[wid];
    xlab_mem_free(thread_conf);


    /* Epoll 事件处理程序 */
	handler = xlab_epoll_set_handlers((void *) xlab_conn_read,
		(void *) xlab_conn_write,
		(void *) xlab_conn_error,
		(void *) xlab_conn_close);
    /* 注册 UDP/UART fd 到本 Worker epoll */
    int udp_fd = xlab_trans_get_fd(XLAB_TRANS_UDP);
    if (udp_fd > 0) {
        int ret = xlab_epoll_add(thinfo->epoll_fd, udp_fd,
                                 XLAB_EPOLL_READ, XLAB_EPOLL_LEVEL_TRIGGERED);
        xlab_info("[EPOLL] UDP fd=%d added to efd=%d, ret=%d", udp_fd, thinfo->epoll_fd, ret);
        if (ret < 0) {
            xlab_warn("Failed to add UDP fd %d to epoll", udp_fd);
        }
    }

    int uart_fd = xlab_trans_get_fd(XLAB_TRANS_UART);
    if (uart_fd > 0) {
        int ret = xlab_epoll_add(thinfo->epoll_fd, uart_fd,
                                 XLAB_EPOLL_READ, XLAB_EPOLL_LEVEL_TRIGGERED);
        if (ret < 0) {
            xlab_warn("Failed to add UART fd %d to epoll", uart_fd);
        }
    }

    /* 使用 thread_key 将 epoll 文件描述符导出到线程上下文 */
    xlab_sched_set_thread_poll(thinfo->epoll_fd);

    /* 将已知的调度器节点导出到线程上下文 */
    pthread_setspecific(worker_sched_node_k, (void *) thinfo);
    /* 1. 注册 eventfd 到本 Worker 的 epoll 实例 */
    int async_efd = xlab_async_get_event_fd();
    if (async_efd > 0) {
        xlab_epoll_add(thinfo->epoll_fd, async_efd, XLAB_EPOLL_READ, XLAB_EPOLL_LEVEL_TRIGGERED);
        xlab_info("Async eventfd %d registered to worker epoll", async_efd);
    }

    /* 2. 进入 epoll 事件循环 */
    xlab_epoll_init(thinfo->epoll_fd, handler, epoll_max_events);

    return 0;
}

/* 注册线程信息。调用线程是线程信息的所有者 */
int xlab_sched_register_thread(int efd)
{
    /* 防御性检查 */
    if (!sched_list) {
        xlab_err("sched_list is NULL! xlab_sched_init() must be called before creating threads");
        return -1;
    }
    int i;
    struct sched_connection *sched_conn, *array;
    struct sched_list_node *sl;
    static int wid = 0;

    /* 如果此线程在此区域休眠，其他线程可能会访问 wid。
     * 因此使用互斥锁保护，只有一个线程可以处理 wid。*/
    pthread_mutex_lock(&mutex_sched_init);

    sl = &sched_list[wid];
    sl->idx = (short)wid++;
    sl->tid = pthread_self();

    /* 在 Linux 中进程和线程没有区别，在内核任务结构中一切都是线程，
     * 每个线程都有自己的数字标识符：PID。
     * 这里我们想知道与此运行任务关联的 PID，
     * 可以通过 gettid() 获取，但 Glibc 不向用户空间导出 syscall，
     * 我们需要直接通过 syscall(2) 调用。*/
    sl->pid = (pid_t)syscall(__NR_gettid);
    sl->epoll_fd = efd;

    pthread_mutex_unlock(&mutex_sched_init);

    xlab_list_init(&sl->busy_queue);
    xlab_list_init(&sl->av_queue);

    array = xlab_mem_malloc_z(sizeof(struct sched_connection) * (size_t)config->worker_capacity);

    for (i = 0; i < config->worker_capacity; i++) {
        sched_conn = &array[i];
        sched_conn->status = XLAB_SCHEDULER_CONN_AVAILABLE;
        sched_conn->socket = -1;
        sched_conn->arrive_time = 0;

        xlab_list_add(&sched_conn->_head, &sl->av_queue);
    }
    sl->request_handler = NULL;

    return sl->idx;
}

/* 创建将监听传入文件描述符的线程 */
int xlab_sched_launch_thread(int max_events)
{
    int efd;
    pthread_t tid;
    pthread_attr_t attr;
    sched_thread_conf *thconf;

    /* 创建 epoll 文件描述符 */
    efd = xlab_epoll_create(max_events);
    if (efd < 1) {
        return -1;
    }

    thconf = xlab_mem_malloc_z(sizeof(sched_thread_conf));
    thconf->epoll_fd = efd;
    thconf->epoll_max_events = max_events*2;
    thconf->max_events = max_events;

    pthread_attr_init(&attr); /*初始化线程的属性*/
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED); /*设置启动状态，当前-分离状态启动；若为：PTHREAD_CREATE_JOINABLE    正常启动线程*/
    if (pthread_create(&tid, &attr, xlab_sched_launch_worker_loop,
                       (void *) thconf) != 0) {
        perror("pthread_create");
        return -1;
    }

    return 0;
}

/* 调度器节点是 struct sched_list_node 类型的数组，
 * 每个 worker 线程属于一个调度器节点，在本函数中我们为定义的 worker 数量分配调度器节点。*/
void xlab_sched_init()
{
    sched_list = xlab_mem_malloc_z(sizeof(struct sched_list_node) *
                                 (size_t)config->workers);
}

struct xlab_list *xlab_sched_get_request_list()
{
    return pthread_getspecific(request_list_k); /* 返回线程中存储的特殊值--request_list-- void *类型的值*/
}

void xlab_sched_set_request_list(struct xlab_list *list)
{
    pthread_setspecific(request_list_k, (void *) list); /* 线程中存储特殊值--*list*/
}

void xlab_sched_set_thread_poll(int epoll)
{
    pthread_setspecific(epoll_fd_k, (void *) (size_t) epoll);
}

int xlab_sched_get_thread_poll()
{
    return (int)(uintptr_t) pthread_getspecific(epoll_fd_k);
}

struct sched_list_node *xlab_sched_get_thread_conf()
{
    return pthread_getspecific(worker_sched_node_k);
}

int xlab_sched_remove_client(struct sched_list_node *sched, int remote_fd)
{
    struct sched_connection *sc;

    /* 关闭 socket 并更改状态：我们不调用 xlab_epoll_del()，
     * 因为 socket 关闭时内核会从队列中清除。*/
    close(remote_fd);

    sc = xlab_sched_get_connection(sched, remote_fd);
    if (sc) {
        sched->closed_connections++;

        /* 更改节点状态 */
        sc->status = XLAB_SCHEDULER_CONN_AVAILABLE;
        sc->socket = -1;

        xlab_list_del(&sc->_head);
        xlab_list_add(&sc->_head, &sched->av_queue);
        /* 新增日志：打印移除的 socket 信息 */
        xlab_info("Socket %d removed from worker %d", remote_fd, sched->idx);
        return 0;
    }
    else {
    }
    return -1;
}

struct sched_connection *xlab_sched_get_connection(struct sched_list_node *sched,
                                                 int remote_fd)
{
    struct xlab_list *head;
    struct sched_connection *entry;

    /* 在某些情况下，sched 节点可能为 NULL，例如提前关闭时，
     * 一个例子是函数 xlab_sched_add_client() 在调用时关闭传入连接，
     * 因此不存在线程上下文。*/
    if (!sched) {
        close(remote_fd);
        return NULL;
    }

    xlab_list_foreach(head, &sched->busy_queue) {
        entry = xlab_list_entry(head, struct sched_connection, _head);
        if (entry->socket == remote_fd) {
            return entry;
        }
    }
    return NULL;
}

int xlab_sched_update_conn_status(struct sched_list_node *sched,
                                int remote_fd, int status)
{
    struct xlab_list *head;
    struct sched_connection *sched_conn;

    if (!sched) {
        return -1;
    }

    xlab_list_foreach(head, &sched->busy_queue) {
        sched_conn = xlab_list_entry(head, struct sched_connection, _head);
        if (sched_conn->socket == remote_fd) {
            sched_conn->status = status;
            return 0;
        }
    }

    return -1;
}