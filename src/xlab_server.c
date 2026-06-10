#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "xlab_config.h"
#include "xlab_scheduler.h"
#include "xlab_epoll.h"
#include "xlab_socket.h"
#include "xlab_utils.h"
#include "xlab_macros.h"

int xlab_server_worker_capacity(int nworkers)
{
    int max, avl;
    struct rlimit lim;

    getrlimit(RLIMIT_NOFILE, &lim);
    max = (int)lim.rlim_cur;
    avl = max - (3 + 1 + nworkers + 1 + 2);
    return ((avl / 2) / nworkers);
}

void xlab_server_launch_workers()
{
    int i;

    for (i = 0; i < config->workers; i++) {
        xlab_sched_launch_thread(config->worker_capacity);
    }
}

void xlab_server_loop(int server_fd)
{
    int ret;
    int remote_fd;

    /* Activate TCP_DEFER_ACCEPT *///减少系统调用
    if (xlab_socket_set_tcp_defer_accept(server_fd) != 0) {
            xlab_warn("TCP_DEFER_ACCEPT failed");
    }

    xlab_info("Epoll Server starting...\n");

    while (1) {
        remote_fd = xlab_socket_accept(server_fd);

        if (remote_fd == -1) {
            continue;
        }
        /* Assign socket to worker thread */
        ret = xlab_sched_add_client(remote_fd);
        if (ret == -1) {
            close(remote_fd);
        }
    }
}
