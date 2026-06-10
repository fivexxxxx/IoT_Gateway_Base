#ifndef XLAB_SERVER_H
#define XLAB_SERVER_H

int xlab_server_worker_capacity(int nworkers);
void xlab_server_launch_workers(void);
void xlab_server_loop(int server_fd);

#endif
