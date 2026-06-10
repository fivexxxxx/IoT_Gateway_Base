#include "xlab_trans.h"
#include "xlab_macros.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

/* UDP 私有上下文 */
typedef struct {
    int sock_fd;
    struct sockaddr_in last_src;  /* 缓存最近一次源地址, 用于响应 */
    socklen_t src_len;
} xlab_udp_priv_t;

static int xlab_udp_read(uint32_t session_id, uint8_t* buf, size_t max_len)
{
    xlab_udp_priv_t* ctx = (xlab_udp_priv_t*)g_trans_ops[XLAB_TRANS_UDP]->priv;
    if (!ctx || ctx->sock_fd < 0) return -1;

    ctx->src_len = sizeof(ctx->last_src);
    int n = (int)recvfrom(ctx->sock_fd, buf, max_len, 0,
        (struct sockaddr*)&ctx->last_src, &ctx->src_len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        xlab_warn("UDP recvfrom error: %s", strerror(errno));
        return -1;
    }
    return n;
}

static int xlab_udp_write(uint32_t session_id, const uint8_t* buf, size_t len)
{
    xlab_udp_priv_t* ctx = (xlab_udp_priv_t*)g_trans_ops[XLAB_TRANS_UDP]->priv;
    if (!ctx || ctx->sock_fd < 0) return -1;

    int n = (int)sendto(ctx->sock_fd, buf, len, 0,
        (struct sockaddr*)&ctx->last_src, ctx->src_len);
    if (n < 0 && errno != EAGAIN) {
        xlab_warn("UDP sendto error: %s", strerror(errno));
    }
    return n;
}

static int xlab_udp_close(uint32_t session_id)
{
    /* UDP 通常不关闭监听套接字, 仅清理状态 */
    return 0;
}

/* 初始化并注册 UDP 驱动 */
int xlab_trans_udp_init(int port)
{
    static xlab_udp_priv_t udp_ctx;
    static xlab_trans_ops_t udp_ops = {
        .type = XLAB_TRANS_UDP,
        .read = xlab_udp_read,
        .write = xlab_udp_write,
        .close = xlab_udp_close,
        .priv = &udp_ctx
    };

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        xlab_err("Failed to create UDP socket");
        return -1;
    }

    /* 设置非阻塞 */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        xlab_err("Failed to bind UDP port %d", port);
        close(fd);
        return -1;
    }

    udp_ctx.sock_fd = fd;
    udp_ctx.src_len = sizeof(udp_ctx.last_src);
    g_trans_fds[XLAB_TRANS_UDP] = fd;

    xlab_trans_register(XLAB_TRANS_UDP, &udp_ops);
    /* 映射固定 session_id */
    extern void trans_fd_map_add(int, uint32_t, xlab_trans_type_e);
    trans_fd_map_add(fd, XLAB_SESSION_ID_UDP, XLAB_TRANS_UDP);

    xlab_info("UDP transport initialized on port %d (fd=%d)", port, fd);
    return 0;
}