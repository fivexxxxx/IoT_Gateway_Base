/**
 * @file bus/cmd_02.c
 * @brief 命令 0x02: 查询设备版本 (非阻塞业务示例)
 *
 * 设计原则:
 * - 仅处理纯内存/状态查询，无硬件阻塞操作
 * - 注册时标记 XLAB_CMD_FLAG_NONE
 * - 返回值: 0=成功, 负值=错误码(自动映射到协议层)
 */

#include "xlab_router.h"
#include "xlab_macros.h"

/* 业务 Handler 实现 (新签名) */
static int cmd_handler_get_version(
    const uint8_t* params,    /* 输入参数 (本命令无参数) */
    uint8_t p_len,            /* 参数长度 */
    uint8_t* resp,           /* 响应数据缓冲 */
    uint8_t* resp_len)       /* 输出: 响应有效长度 */
{
    /* 参数校验 (防御) */
    if (p_len != 0) {
        xlab_warn("cmd_02: unexpected params len=%d", p_len);
        return -XLAB_ERR_PARAM_RANGE;
    }

    /* 业务逻辑: 填充版本信息 (示例) */
    resp[0] = 0x01;  /* 主版本号 */
    resp[1] = 0x02;  /* 次版本号 */
    resp[2] = 0x00;  /* 修订号 */
    *resp_len = 3;

    xlab_info("cmd_02 executed: version=%d.%d.%d", resp[0], resp[1], resp[2]);
    return 0;  /* 成功 */
}

/* 模块自动注册 (编译时生效) */
__attribute__((constructor))
static void cmd_02_module_init(void)
{
    xlab_router_register(
        XLAB_CMD_GET_VER,           /* 命令码 */
        cmd_handler_get_version,    /* Handler 函数指针 */
        "Get device version",      /* 描述 (调试用) */
        0, 0,                       /* 参数长度约束 [min, max] */
        XLAB_CMD_FLAG_NONE          /* 标志: 非阻塞 */
    );
}