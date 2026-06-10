#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "xlab_list.h"

#ifndef XLAB_SCHEDULER_H
#define XLAB_SCHEDULER_H

#define XLAB_SCHEDULER_CONN_AVAILABLE -1
#define XLAB_SCHEDULER_CONN_PENDING 0
#define XLAB_SCHEDULER_CONN_PROCESS 1

struct sched_connection
{
    int status;
    int socket;

    time_t arrive_time;

    struct xlab_list _head;
};

/* Global struct */
struct sched_list_node
{
    unsigned long long accepted_connections;
    unsigned long long closed_connections;

    struct xlab_list busy_queue;
    struct xlab_list av_queue;

    short int idx;
    pthread_t tid;
    pid_t pid;
    int epoll_fd;
    unsigned char initialized;
    struct xlab_client_session *request_handler;
};

struct sched_list_node *sched_list;

/* Struct under thread context */
typedef struct
{
    int epoll_fd;
    int epoll_max_events;
    int max_events;
} sched_thread_conf;

pthread_key_t epoll_fd_k;
pthread_key_t worker_sched_node_k;

extern pthread_mutex_t mutex_worker_init;

void xlab_sched_init();
int xlab_sched_register_thread(int epoll_fd);
int xlab_sched_launch_thread(int max_events);
struct xlab_list *xlab_sched_get_request_list(void);
void xlab_sched_set_request_list(struct xlab_list *list);
int xlab_sched_get_thread_poll(void);
void xlab_sched_set_thread_poll(int epoll);
struct sched_list_node *xlab_sched_get_thread_conf(void);
int xlab_sched_add_client(int remote_fd);
int xlab_sched_register_client(int remote_fd, struct sched_list_node *sched);
int xlab_sched_remove_client(struct sched_list_node *sched, int remote_fd);
struct sched_connection *xlab_sched_get_connection(struct sched_list_node
                                                 *sched, int remote_fd);
int xlab_sched_update_conn_status(struct sched_list_node *sched, int remote_fd,
                                int status);

#endif
