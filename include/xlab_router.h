#ifndef XLAB_ROUTER_H
#define XLAB_ROUTER_H

#include "xlab_proto_parser.h"
#include <stdint.h>

typedef enum {
    XLAB_ERR_OK = 0x00,
    XLAB_ERR_CHECKSUM = 0x01,
    XLAB_ERR_LEN_OVERFLOW = 0x02,
    XLAB_ERR_UNKNOWN_CMD = 0x03,
    XLAB_ERR_PARAM_RANGE = 0x04,
    XLAB_ERR_BUS_TIMEOUT = 0x10,
    XLAB_ERR_BUS_HARDWARE = 0x11,
    XLAB_ERR_BUS_BUSY = 0x12,
    XLAB_ERR_INTERNAL = 0xFF
} xlab_err_code_e;

#define XLAB_CMD_FLAG_NONE      0x00
#define XLAB_CMD_FLAG_BLOCKING  0x01

typedef int (*xlab_cmd_handler_t)(
    const uint8_t* params, uint8_t p_len,
    uint8_t* resp, uint8_t* resp_len);

typedef struct {
    xlab_packte_cmd_e cmd_code;
    xlab_cmd_handler_t handler;
    const char* desc;
    uint8_t min_params;
    uint8_t max_params;
    uint8_t flags;
} xlab_router_entry_t;

int xlab_router_register(xlab_packte_cmd_e cmd_code, xlab_cmd_handler_t handler,
    const char* desc, uint8_t min_params, uint8_t max_params, uint8_t flags);
int xlab_router_process(xlab_packte_cmd_e cmd_code, const uint8_t* payload, uint8_t p_len,
    uint32_t seq, uint8_t* resp_buf, uint8_t* resp_len);
int xlab_router_build_response(xlab_packte_cmd_e cmd_code, xlab_err_code_e err_code,
    const uint8_t* data, uint8_t data_len, uint32_t seq,
    uint8_t* out_buf, uint8_t* out_len);
xlab_router_entry_t* xlab_router_lookup(xlab_packte_cmd_e cmd);

#endif /* XLAB_ROUTER_H */