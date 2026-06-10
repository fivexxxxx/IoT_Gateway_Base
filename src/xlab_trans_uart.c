#include "xlab_trans.h"
#include "xlab_macros.h"
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

/* UART 私有上下文 */
typedef struct {
    int fd;
    char device[32];
    speed_t baudrate;
} xlab_uart_priv_t;

static int xlab_uart_read(uint32_t session_id, uint8_t* buf, size_t max_len)
{
    xlab_uart_priv_t* ctx = (xlab_uart_priv_t*)g_trans_ops[XLAB_TRANS_UART]->priv;
    if (!ctx || ctx->fd < 0) return -1;

    int n = (int)read(ctx->fd, buf, max_len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        xlab_warn("UART read error: %s", strerror(errno));
        return -1;
    }
    return n;
}

static int xlab_uart_write(uint32_t session_id, const uint8_t* buf, size_t len)
{
    xlab_uart_priv_t* ctx = (xlab_uart_priv_t*)g_trans_ops[XLAB_TRANS_UART]->priv;
    if (!ctx || ctx->fd < 0) return -1;

    int n = (int)write(ctx->fd, buf, len);
    if (n < 0 && errno != EAGAIN) {
        xlab_warn("UART write error: %s", strerror(errno));
    }
    return n;
}

static int xlab_uart_close(uint32_t session_id)
{
    xlab_uart_priv_t* ctx = (xlab_uart_priv_t*)g_trans_ops[XLAB_TRANS_UART]->priv;
    if (ctx && ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }
    return 0;
}

/* 初始化并注册 UART 驱动 */
int xlab_trans_uart_init(const char* dev_path, speed_t baud)
{
    static xlab_uart_priv_t uart_ctx;
    static xlab_trans_ops_t uart_ops = {
        .type = XLAB_TRANS_UART,
        .read = xlab_uart_read,
        .write = xlab_uart_write,
        .close = xlab_uart_close,
        .priv = &uart_ctx
    };

    strncpy(uart_ctx.device, dev_path, sizeof(uart_ctx.device) - 1);
    uart_ctx.baudrate = baud;

    int fd = open(dev_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        xlab_err("Failed to open UART device %s: %s", dev_path, strerror(errno));
        return -1;
    }

    /* 配置终端属性 */
    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd, &tty) != 0) {
        xlab_warn("Failed to get UART attributes, using defaults");
        return -1;
    }

    cfsetospeed(&tty, baud);
    cfsetispeed(&tty, baud);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(tcflag_t)PARENB;
    tty.c_cflag &= ~(tcflag_t)CSTOPB;
    tty.c_cflag &= ~(tcflag_t)CSIZE;
    tty.c_cflag |= CS8;
    tty.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(tcflag_t)(IXON | IXOFF | IXANY);
    tty.c_oflag &= ~(tcflag_t)OPOST;
    tty.c_cc[VMIN] = 0;  /* 非阻塞读 */
    tty.c_cc[VTIME] = 1; /* 0.1秒超时 */

    tcsetattr(fd, TCSANOW, &tty);
    tcflush(fd, TCIFLUSH);

    uart_ctx.fd = fd;
    g_trans_fds[XLAB_TRANS_UART] = fd;

    xlab_trans_register(XLAB_TRANS_UART, &uart_ops);
    extern void trans_fd_map_add(int, uint32_t, xlab_trans_type_e);
    trans_fd_map_add(fd, XLAB_SESSION_ID_UART, XLAB_TRANS_UART);

    xlab_info("UART transport initialized: %s @ %d baud (fd=%d)", dev_path, baud, fd);
    return 0;
}