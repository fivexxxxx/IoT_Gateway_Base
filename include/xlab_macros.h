#ifndef XLAB_MACROS_H
#define XLAB_MACROS_H

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "xlab_limits.h"
extern void xlab_print(int level, const char* fmt, ...);
/* Boolean */
#define XLAB_FALSE 0
#define XLAB_TRUE  !XLAB_FALSE
#define XLAB_ERROR -1

/* Architecture */
#define INTSIZE sizeof(int)

/* Print macros */
#define XLAB_INFO     0x1000
#define XLAB_ERR      0X1001
#define XLAB_WARN     0x1002
#define XLAB_BUG      0x1003

#define xlab_info(...)  xlab_print(XLAB_INFO, __VA_ARGS__)
#define xlab_err(...)   xlab_print(XLAB_ERR, __VA_ARGS__)
#define xlab_warn(...)  xlab_print(XLAB_WARN, __VA_ARGS__)


/* ANSI Colors */
#define ANSI_BOLD "\033[1m"
#define ANSI_CYAN "\033[36m"
#define ANSI_MAGENTA "\033[35m"
#define ANSI_RED "\033[31m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_BLUE "\033[34m"
#define ANSI_GREEN "\033[32m"
#define ANSI_WHITE "\033[37m"
#define ANSI_RESET "\033[0m"


#ifndef ARRAY_SIZE
# define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#define xlab_unlikely(x) __builtin_expect((x),0)

#define xlab_is_bool(x) ((x == XLAB_TRUE || x == XLAB_FALSE) ? 1 : 0)

#define xlab_bug(condition) do {                                          \
        if (xlab_unlikely((condition)!=0)) {                              \
            xlab_print(XLAB_BUG, "Bug found in %s() at %s:%d",              \
                     __FUNCTION__, __FILE__, __LINE__);                 \
            abort();                                                    \
        }                                                               \
    } while(0)

#endif
/* ==========================================================================
 * 调试日志增强 (调试阶段启用，生产阶段剔除)
 * ========================================================================== */
 /* 调试开关: 定义则启用详细调试日志 */
#define XLAB_DEBUG_TRACE

#ifdef XLAB_DEBUG_TRACE
/* 打印十六进制数据 (用于调试原始报文) */
static inline void xlab_print_hex(const char* label, const uint8_t * buf, uint8_t len)
{
    fprintf(stderr, "[HEX:%s] ", label);
    for (uint8_t i = 0; i < len; i++) {
        fprintf(stderr, "%02X ", buf[i]);
    }
    fprintf(stderr, "\n");
    fflush(stderr);
}

/* 调试专用日志 (带文件/行号) */
#define xlab_dbg(fmt, ...) \
    xlab_print(XLAB_INFO, "[DBG:%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define xlab_print_hex(label, buf, len) do {} while(0)
#define xlab_dbg(fmt, ...) do {} while(0)
#endif