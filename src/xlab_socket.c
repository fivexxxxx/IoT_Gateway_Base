#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include "xlab_socket.h"
#include "xlab_limits.h"
#include "xlab_macros.h"
#include "xlab_string.h"
#include "xlab_memory.h"
#include "xlab_utils.h"

int xlab_socket_set_tcp_nodelay(int sockfd)
{
    int on = 1;
	return setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
}

int xlab_socket_set_tcp_defer_accept(int sockfd)
{
    int timeout = 0;
    return setsockopt(sockfd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &timeout, sizeof(int));
}

int xlab_socket_close(int socket)
{
    return close(socket);
}

int xlab_socket_create()
{
    int sockfd;
    if ((sockfd = socket(AF_INET6, SOCK_STREAM, 0)) == -1) {
        perror("client: socket");
        return -1;
    }
    return sockfd;
}
int xlab_create_socket(int domain, int type, int protocol)
{
	int socket_fd;
	socket_fd = socket(domain, type, protocol);
	return socket_fd;
}
int xlab_socket_bind(int socket_fd, const struct sockaddr* addr, socklen_t addrlen, int backlog)
{
	int ret;
	ret = bind(socket_fd, addr, addrlen);

	if (ret == -1) {
		xlab_warn("Error binding socket");
		return ret;
	}
	ret = listen(socket_fd, backlog);
	if (ret == -1) {
		xlab_warn("Error setting up the listener");
		return -1;
	}
	return ret;
}
int xlab_socket_connect(char *host, int port)
{
	int ret;
	int socket_fd = -1;
	char* port_str = 0;
	unsigned long len;
	struct addrinfo hints;
	struct addrinfo* res, * rp;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	xlab_string_build(&port_str, &len, "%d", port);
	ret = getaddrinfo(host, port_str, &hints, &res);
	xlab_mem_free(port_str);
	if (ret != 0) {
		xlab_err("Can't get addr info: %s", gai_strerror(ret));
		return -1;
	}
	for (rp = res; rp != NULL; rp = rp->ai_next) {
		socket_fd = xlab_create_socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

		if (socket_fd == -1) {
			xlab_warn("Error creating client socket");
			return -1;
		}
		if (connect(socket_fd,
			(struct sockaddr*) rp->ai_addr, rp->ai_addrlen) == -1) {
			close(socket_fd);
			xlab_err("Can't connect to %s", host);
			return -1;
		}
		break;
	}
	return socket_fd;
}

int xlab_socket_reset(int socket)
{
    int status = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &status, sizeof(int)) ==
        -1) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    return 0;
}

/* IPv4 for now... */
int xlab_socket_server(int port, char *listen_addr)
{
	int socket_fd = -1;
	int ret;
	char* port_str = 0;
	unsigned long len;
	struct addrinfo hints;
	struct addrinfo* res, * rp;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	xlab_string_build(&port_str, &len, "%d", port);
	ret = getaddrinfo(listen_addr, port_str, &hints, &res);
	xlab_mem_free(port_str);
	if (ret != 0) {
		xlab_err("Can't get addr info: %s", gai_strerror(ret));
		return -1;
	}
	for (rp = res; rp != NULL; rp = rp->ai_next) {
		socket_fd = xlab_create_socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

		if (socket_fd == -1) {
			xlab_warn("Error creating server socket");
			return -1;
		}
		xlab_socket_set_tcp_nodelay(socket_fd);
		xlab_socket_reset(socket_fd);
		ret = xlab_socket_bind(socket_fd, rp->ai_addr, rp->ai_addrlen, XLAB_SOMAXCONN);

		if (ret == -1) {
			xlab_err("Port %i cannot be used\n", port);
			return -1;
		}
		break;
	}
	return socket_fd;

}

int xlab_socket_accept(int server_fd)
{
	int remote_fd;
	struct sockaddr sock_addr;
	socklen_t socket_size = sizeof(struct sockaddr);

#ifdef ACCEPT_GENERIC
	remote_fd = accept(server_fd, &sock_addr, &socket_size);
	socket_set_nonblocking(remote_fd);
#else
	remote_fd = accept4(server_fd, &sock_addr, &socket_size, SOCK_NONBLOCK);
#endif
	return remote_fd;
}

int xlab_socket_send(int socket_fd, const void *buf, size_t count)
{
	ssize_t bytes_sent = -1;
	bytes_sent = write(socket_fd, buf, count);
	return (int)bytes_sent;
}

int xlab_socket_read(int socket_fd, void *buf, int count)
{
	ssize_t bytes_read;
	bytes_read = read(socket_fd, (void*)buf, (size_t)count);
	return (int)bytes_read;
}