#ifndef XLAB_PROTO_PARSER_H
#define XLAB_PROTO_PARSER_H

#include "xlab_commands.h"
#include <stdint.h>
#include <stddef.h>

#define XLAB_PROTO_HEAD_SIZE        1
#define XLAB_PROTO_LEN_SIZE         1
#define XLAB_PROTO_CMD_SIZE         1
#define XLAB_PROTO_CHK_SIZE         1
#define XLAB_PROTO_FIXED_OVERHEAD   4
#define XLAB_PROTO_PAYLOAD_MAX      251
#define XLAB_PROTO_FRAME_MAX        255
#define XLAB_RESP_BUF_SIZE          256

typedef enum {
    XLAB_PARSE_WAIT_HEAD = 0,
    XLAB_PARSE_WAIT_LEN = 1,
    XLAB_PARSE_WAIT_CMD = 2,
    XLAB_PARSE_WAIT_PAYLOAD = 3,
    XLAB_PARSE_WAIT_CHECK = 4
} xlab_parse_state_e;

typedef struct {
    xlab_parse_state_e state;
    uint8_t frame_buf[XLAB_PROTO_FRAME_MAX];
    uint8_t buf_pos;
    uint8_t payload_len;
    uint8_t expected_total;
    uint32_t next_req_seq;
} xlab_proto_ctx_t;

typedef enum {
    XLAB_PARSE_OK = 1,
    XLAB_PARSE_INCOMPLETE = 0,
    XLAB_PARSE_CHK_ERR = -1,
    XLAB_PARSE_ERROR = -2
} xlab_parse_result_e;

xlab_parse_result_e xlab_proto_parse_byte(
    xlab_proto_ctx_t* ctx, uint8_t byte,
    xlab_packte_cmd_e* out_cmd, uint8_t* out_payload,
    uint8_t* out_len, uint32_t* out_seq);

/* 校验和计算: 二进制补码算法 (与客户端对齐) */
static inline uint8_t xlab_proto_calc_checksum(const uint8_t* buf, uint8_t len)
{
    uint32_t sum = 0;
    /* 累加前 len-1 字节（排除校验和字节本身） */
    for (uint8_t i = 0; i < len - 1; i++) {
        sum += buf[i];
    }
    /* 二进制补码: 按位取反 +1 */
    return (uint8_t)(~sum + 1);
}

#endif /* XLAB_PROTO_PARSER_H */