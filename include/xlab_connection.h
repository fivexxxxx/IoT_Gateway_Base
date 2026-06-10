#ifndef XLAB_CONNECTION_H
#define XLAB_CONNECTION_H

int xlab_conn_read(int socket);
int xlab_conn_write(int socket);
int xlab_conn_error(int socket);
int xlab_conn_close(int socket);

#endif
