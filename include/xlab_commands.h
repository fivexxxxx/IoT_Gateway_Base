#ifndef XLAB_COMMANDS_H
#define XLAB_COMMANDS_H

#include <stdint.h>
//#include "xlab_router.h"  /* 引入新路由层，自动包含 xlab_cmd_handler_t */

#pragma pack(push,1)
typedef enum {
    XLAB_BOOT_CODE_HEAD = 0x40,
    XLAB_RETURN_CODE_OK = 0xF0,
    XLAB_RETURN_CODE_ERR = 0xF4
} xlab_packet_boot_code_e;

typedef enum {
    NONE = 0x00,
    XLAB_CMD_GET_VER = 0x02,
    XLAB_CMD_START_READ = 0x06
} xlab_packte_cmd_e;
#pragma pack(pop)

/* 旧接口已废弃，全局统一使用 xlab_router_register */
#endif