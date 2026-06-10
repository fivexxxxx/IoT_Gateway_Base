#include "xlab_trans.h"
#include "xlab_macros.h"
#include "xlab_request.h"
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/* 全局驱动注册表 */
xlab_trans_ops_t* g_trans_ops[XLAB_TRANS_MAX] = { 0 };
int g_trans_fds[XLAB_TRANS_MAX] = { -1, -1, -1 };

/* fd -> session_id 简单映射表 */
#define TRANS_FD_MAP_SIZE 32
static struct {
    int fd;
    uint32_t session_id;
    xlab_trans_type_e type;
} g_fd_map[TRANS_FD_MAP_SIZE];
static int g_fd_map_count = 0;

int xlab_trans_register(xlab_trans_type_e type, xlab_trans_ops_t* ops)
{
    if (type >= XLAB_TRANS_MAX || !ops) return -1;
    g_trans_ops[type] = ops;
    return 0;
}

int xlab_trans_get_fd(xlab_trans_type_e type)
{
    return (type < XLAB_TRANS_MAX) ? g_trans_fds[type] : -1;
}

void trans_fd_map_add(int fd, uint32_t sid, xlab_trans_type_e type)
{
    if (g_fd_map_count < TRANS_FD_MAP_SIZE) {
        g_fd_map[g_fd_map_count].fd = fd;
        g_fd_map[g_fd_map_count].session_id = sid;
        g_fd_map[g_fd_map_count].type = type;
        g_fd_map_count++;
    }
}

uint32_t xlab_session_map_by_fd(int fd)
{
    for (int i = 0; i < g_fd_map_count; i++) {
        if (g_fd_map[i].fd == fd) return g_fd_map[i].session_id;
    }
    return 0; /* 未找到 */
}

/* 框架统一分发接口 (供 xlab_conn_read/write 调用) */
int xlab_trans_read_dispatch(int fd, uint8_t* buf, size_t max_len)
{
    uint32_t sid = xlab_session_map_by_fd(fd);
    if (sid == 0) return -1;

    for (int i = 0; i < XLAB_TRANS_MAX; i++) {
        if (g_trans_ops[i] && g_trans_ops[i]->read) {
            /* 简单匹配: UDP/UART 用固定sid, TCP用fd映射 */
            if (sid == XLAB_SESSION_ID_UDP || sid == XLAB_SESSION_ID_UART ||
                (sid == (uint32_t)fd)) { /* TCP兼容 */
                return g_trans_ops[i]->read(sid, buf, max_len);
            }
        }
    }
    return -1;
}

int xlab_trans_write_dispatch(int fd, const uint8_t* buf, size_t len)
{
    uint32_t sid = xlab_session_map_by_fd(fd);
    if (sid == 0) return -1;

    for (int i = 0; i < XLAB_TRANS_MAX; i++) {
        if (g_trans_ops[i] && g_trans_ops[i]->write) {
            if (sid == XLAB_SESSION_ID_UDP || sid == XLAB_SESSION_ID_UART ||
                (sid == (uint32_t)fd)) {
                return g_trans_ops[i]->write(sid, buf, len);
            }
        }
    }
    return -1;
}
/* 清理 fd 映射表 */
void trans_fd_map_remove(int fd)
{
    for (int i = 0; i < g_fd_map_count; i++) {
        if (g_fd_map[i].fd == fd) {
            /* 后移覆盖删除 */
            for (int j = i; j < g_fd_map_count - 1; j++) {
                g_fd_map[j] = g_fd_map[j + 1];
            }
            g_fd_map_count--;
            break;
        }
    }
}