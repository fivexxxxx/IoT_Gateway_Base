#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
/**
 * @file bus/cmd_06.c
 * @brief 命令 0x06: 启动标签读取 (阻塞业务示例)
 *
 * 设计原则:
 * - 涉及 UART/SPI 硬件交互，可能阻塞
 * - 注册时标记 XLAB_CMD_FLAG_BLOCKING，框架自动派发至异步队列
 * - Handler 内部可安全调用阻塞函数，不影响 epoll 循环
 */

#include "xlab_router.h"
#include "xlab_macros.h"
#include <string.h>
#include <unistd.h>  /* 仅示例用，实际应调用硬件驱动 */

/* 模拟硬件读取函数 (实际应替换为真实驱动) */
static int hw_read_tag_sync(const uint8_t* cmd, uint8_t cmd_len,
    uint8_t* out_data, uint8_t* out_len)
{
    /* 模拟 UART 交互延时 (实际会阻塞 10~100ms) */
    usleep(30000);  /* 30ms */

    /* 模拟返回标签数据 */
    out_data[0] = 0xAA;
    out_data[1] = 0xBB;
    out_data[2] = 0xCC;
    *out_len = 3;

    return 0;  /* 成功 */
}

/* 业务 Handler 实现 */
static int cmd_handler_start_read(
    const uint8_t* params,
    uint8_t p_len,
    uint8_t* resp,
    uint8_t* resp_len)
{
    /* 参数校验: 本命令需 2 字节参数 (设备ID) */
    if (p_len < 2) {
        return -XLAB_ERR_PARAM_RANGE;
    }
    uint8_t device_id = (uint8_t)((params[0] << 8) | params[1]);
    xlab_info("cmd_06: start read for device_id=0x%04X", device_id);

    /* 调用硬件交互 (可能阻塞) */
    uint8_t tag_data[64];
    uint8_t tag_len = 0;
    int ret = hw_read_tag_sync(params, p_len, tag_data, &tag_len);

    if (ret < 0) {
        xlab_warn("cmd_06: hw_read_tag_sync failed, ret=%d", ret);
        return -XLAB_ERR_BUS_HARDWARE;
    }

    /* 填充响应: 返回标签数据 */
    if (tag_len > 0) {
        memcpy(resp, tag_data, tag_len);
        *resp_len = tag_len;
    }
    else {
        *resp_len = 0;
    }

    return 0;
}

/* 模块自动注册 */
__attribute__((constructor))
static void cmd_06_module_init(void)
{
    xlab_router_register(
        XLAB_CMD_START_READ,        /* 命令码 */
        cmd_handler_start_read,     /* Handler */
        "Start RFID tag reading",   /* 描述 */
        2, 4,                       /* 参数长度约束 [2,4] 字节 */
        XLAB_CMD_FLAG_BLOCKING      /* 标志: 阻塞型，自动异步化 */
    );
}