#include <sys/epoll.h>

#ifndef XLAB_EPOLL_H
#define XLAB_EPOLL_H
#define XLAB_EPOLL_READ 0
#define XLAB_EPOLL_WRITE 1
#define XLAB_EPOLL_RW 2

/* Epoll timeout is 3 seconds */
#define XLAB_EPOLL_WAIT_TIMEOUT 3000

#define XLAB_EPOLL_LEVEL_TRIGGERED 2        /* default */
#define XLAB_EPOLL_EDGE_TRIGGERED  EPOLLET

#ifndef EPOLLRDHUP
#define EPOLLRDHUP 0x2000
#endif

typedef struct
{
    int (*read) (int);
    int (*write) (int);
    int (*error) (int);
    int (*close) (int);
} xlab_epoll_handlers;

int xlab_epoll_create(int max_events);
void *xlab_epoll_init(int efd, xlab_epoll_handlers * handler, int max_events);

xlab_epoll_handlers *xlab_epoll_set_handlers(void(*read)(int),
	void(*write)(int),
	void(*error)(int),
	void(*close)(int));                                        
int xlab_epoll_add(int efd, int fd, int mode, int behavior);
int xlab_epoll_del(int efd, int fd); 
int xlab_epoll_change_mode(int efd, int fd, int mode, int behavior);

#endif
