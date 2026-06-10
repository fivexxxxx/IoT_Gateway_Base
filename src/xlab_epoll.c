#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#include "xlab_socket.h"
#include "xlab_request.h"
#include "xlab_config.h"
#include "xlab_scheduler.h"
#include "xlab_epoll.h"
#include "xlab_utils.h"
#include "xlab_macros.h"

xlab_epoll_handlers *xlab_epoll_set_handlers(void (*read) (int),
                                         void (*write) (int),
                                         void (*error) (int),
                                         void (*close) (int))
{
    xlab_epoll_handlers *handler;

    handler = malloc(sizeof(xlab_epoll_handlers));
    handler->read = (void *) read;
    handler->write = (void *) write;
    handler->error = (void *) error;
    handler->close = (void *) close;
    return handler;
}

int xlab_epoll_create(int max_events)
{
    int efd;

    efd = epoll_create(max_events);
    if (efd == -1) {
        perror("epoll_create");
        xlab_err("epoll_create() failed");
    }

    return efd;
}

void *xlab_epoll_init(int efd, xlab_epoll_handlers * handler, int max_events)
{
    int i, fd, ret = -1;
    int num_fds;
    struct epoll_event *events;
    struct sched_list_node *sched;

    /* Get thread conf */
    sched = xlab_sched_get_thread_conf();
    events = xlab_mem_malloc_z(max_events*sizeof(struct epoll_event));

    pthread_mutex_lock(&mutex_worker_init);
    sched->initialized = 1;
    pthread_mutex_unlock(&mutex_worker_init);

    while (1) {
        ret = -1;
        num_fds = epoll_wait(efd, events, max_events, XLAB_EPOLL_WAIT_TIMEOUT);

        for (i = 0; i < num_fds; i++) {
            fd = events[i].data.fd;

            if (events[i].events & EPOLLIN) {
                ret = (*handler->read) (fd);
            }
            else if (events[i].events & EPOLLOUT) {
                ret = (*handler->write) (fd);
            }
            else if (events[i].events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) {
                ret = (*handler->error) (fd);
            }
            if (ret < 0) {
                (*handler->close) (fd);
            }
        }
    }
}

int xlab_epoll_add(int efd, int fd, int init_mode, int behavior)
{
    int ret;
    struct epoll_event event = {0, {0}};

    event.data.fd = fd;
    event.events = EPOLLERR | EPOLLHUP | EPOLLRDHUP;

    if (behavior == (int)XLAB_EPOLL_EDGE_TRIGGERED) {
        event.events |= EPOLLET;
    }

    switch (init_mode) {
    case XLAB_EPOLL_READ:
        event.events |= EPOLLIN;
        break;
    case XLAB_EPOLL_WRITE:
        event.events |= EPOLLOUT;
        break;
    case XLAB_EPOLL_RW:
        event.events |= EPOLLIN | EPOLLOUT;
        break;
    }

    ret = epoll_ctl(efd, EPOLL_CTL_ADD, fd, &event);
    if (ret < 0 && errno != EEXIST) {
        return ret;
    }
    return ret;
}

int xlab_epoll_del(int efd, int fd)
{
    int ret;

    ret = epoll_ctl(efd, EPOLL_CTL_DEL, fd, NULL);

    if (ret < 0) {
        perror("epoll_ctl");
    }
    return ret;
}

int xlab_epoll_change_mode(int efd, int fd, int mode, int behavior)
{
    int ret;
    struct epoll_event event = {0, {0}};

    event.events = EPOLLERR | EPOLLHUP;
    event.data.fd = fd;

    if (behavior == (int)XLAB_EPOLL_EDGE_TRIGGERED) {
        event.events |= EPOLLET;
    }

    switch (mode) {
    case XLAB_EPOLL_READ:
        event.events |= EPOLLIN;
        break;
    case XLAB_EPOLL_WRITE:
        event.events |= EPOLLOUT;
        break;
    case XLAB_EPOLL_RW:
        event.events |= EPOLLIN | EPOLLOUT;
        break;
    }

    ret = epoll_ctl(efd, EPOLL_CTL_MOD, fd, &event);
    if (ret < 0) {
        perror("epoll_ctl");
    }
    return ret;
}
