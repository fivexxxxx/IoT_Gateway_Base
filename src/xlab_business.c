#include "xlab_commands.h"
#include "xlab_utils.h"
#include "xlab_macros.h"
#include "xlab_router.h"

/* === 命令 0x02: 查询版本 (非阻塞示例) === */
__attribute__((__unused__))
static int cmd_handler_get_version(const uint8_t* params, uint8_t p_len,
    uint8_t* resp, uint8_t* resp_len)
{
    /* 业务逻辑: 读取固件版本等, 填充 resp */
    resp[0] = 0x01;  /* 主版本 */
    resp[1] = 0x00;  /* 次版本 */
    *resp_len = 2;
    return 0;
}

/* === 命令 0x06: 启动读取 (阻塞示例, 需异步化) === */
__attribute__((__unused__))
static int cmd_handler_start_read(const uint8_t* params, uint8_t p_len,
    uint8_t* resp, uint8_t* resp_len)
{
    /* 模拟硬件交互延时/阻塞 */
    /* 实际应返回负错误码触发 XLAB_CMD_FLAG_BLOCKING 异步派发 */
    resp[0] = 0xAA;
    resp[1] = 0xBB;
    *resp_len = 2;
    return 0;
}

/* === 命令初始化: 当前没用,命令已经实现了自注册,保留以后可用数据库,权限表等 === */
void xlab_business_init(void)
{
    /* 参数: 命令码, Handler, 描述, 最小参长, 最大参长, 标志位 XLAB_CMD_FLAG_BLOCKING */
    //xlab_router_register(XLAB_CMD_GET_VER, cmd_handler_get_version, "查询版本", 0, 0, XLAB_CMD_FLAG_NONE);
    //xlab_router_register(XLAB_CMD_START_READ, cmd_handler_start_read, "启动读取", 2, 4, XLAB_CMD_FLAG_BLOCKING);

    xlab_info("Business commands registered successfully");
}