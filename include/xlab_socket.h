#ifndef XLAB_SOCKET_H
#define XLAB_SOCKET_H

#include <sys/uio.h>
#include <netinet/in.h>
#include <sys/socket.h>

#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK 04000
#endif

int xlab_socket_set_tcp_nodelay(int sockfd);
int xlab_socket_set_tcp_defer_accept(int sockfd);
int xlab_socket_close(int socket);
int xlab_socket_create(void);
int xlab_socket_connect(char *host, int port);
int xlab_socket_reset(int socket);
int xlab_socket_server(int port, char *listen_addr);
int xlab_socket_accept(int server_fd);
int xlab_socket_send(int socket_fd, const void *buf, size_t count);
int xlab_socket_read(int socket_fd, void *buf, int count);
#endif
