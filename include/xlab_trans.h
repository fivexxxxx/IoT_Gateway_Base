#ifndef XLAB_TRANS_H
#define XLAB_TRANS_H

#include <stdint.h>
#include <stddef.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <termios.h>

#define XLAB_SESSION_ID_UDP  0xFFFFFFFEU
#define XLAB_SESSION_ID_UART 0xFFFFFFFFU

typedef enum {
    XLAB_TRANS_TCP = 0,
    XLAB_TRANS_UDP,
    XLAB_TRANS_UART,
    XLAB_TRANS_MAX
} xlab_trans_type_e;

typedef struct {
    xlab_trans_type_e type;
    int (*read)(uint32_t session_id, uint8_t* buf, size_t max_len);
    int (*write)(uint32_t session_id, const uint8_t* buf, size_t len);
    int (*close)(uint32_t session_id);
    void* priv;
} xlab_trans_ops_t;

/* 暴露给驱动文件 */
extern xlab_trans_ops_t* g_trans_ops[XLAB_TRANS_MAX];
extern int g_trans_fds[XLAB_TRANS_MAX];

int xlab_trans_udp_init(int port);
int xlab_trans_register(xlab_trans_type_e type, xlab_trans_ops_t* ops);
int xlab_trans_get_fd(xlab_trans_type_e type);
int xlab_trans_read_dispatch(int fd, uint8_t* buf, size_t max_len);
int xlab_trans_write_dispatch(int fd, const uint8_t* buf, size_t len);
uint32_t xlab_session_map_by_fd(int fd);
void trans_fd_map_add(int fd, uint32_t sid, xlab_trans_type_e type);
void trans_fd_map_remove(int fd);  /* 清理映射表 */
#endif /* XLAB_TRANS_H */