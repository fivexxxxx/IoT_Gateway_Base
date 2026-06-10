#include "xlab_router.h"
#include "xlab_macros.h"
#include <string.h>

#define XLAB_ROUTER_MAX_CMDS 32
static xlab_router_entry_t s_cmd_table[XLAB_ROUTER_MAX_CMDS];
static int s_cmd_count = 0;

int xlab_router_register(
    xlab_packte_cmd_e cmd_code,
    xlab_cmd_handler_t handler,
    const char* desc,
    uint8_t min_params,
    uint8_t max_params,
    uint8_t flags)
{
    if (s_cmd_count >= XLAB_ROUTER_MAX_CMDS) {
        xlab_err("Router table full, cannot register cmd 0x%02X", cmd_code);
        return -1;
    }
    s_cmd_table[s_cmd_count].cmd_code = cmd_code;
    s_cmd_table[s_cmd_count].handler = handler;
    s_cmd_table[s_cmd_count].desc = desc;
    s_cmd_table[s_cmd_count].min_params = min_params;
    s_cmd_table[s_cmd_count].max_params = max_params;
    s_cmd_table[s_cmd_count].flags = flags;
    s_cmd_count++;
    return 0;
}
xlab_router_entry_t* xlab_router_lookup(xlab_packte_cmd_e cmd)
{
    for (int i = 0; i < s_cmd_count; i++) {
        if (s_cmd_table[i].cmd_code == cmd) {
            return &s_cmd_table[i];
        }
    }
    return NULL;
}

int xlab_router_build_response(
    xlab_packte_cmd_e cmd_code,
    xlab_err_code_e err_code,
    const uint8_t* data,
    uint8_t data_len,
    uint32_t seq,
    uint8_t* out_buf,
    uint8_t* out_len)
{
    uint8_t head = (err_code == XLAB_ERR_OK) ? XLAB_RETURN_CODE_OK : XLAB_RETURN_CODE_ERR;
    /* 调试日志：打印响应构建过程 */
#ifdef XLAB_DEBUG_TRACE
    /*xlab_dbg("Build Response: head=0x%02X, cmd=0x%02X, err=0x%02X, data_len=%d",
        head, cmd_code, err_code, data_len);*/
#endif
    out_buf[0] = head;
    out_buf[2] = (uint8_t)cmd_code;
    uint8_t chk_pos;

    if (head == XLAB_RETURN_CODE_OK) {
        /* 成功响应: [0xF0][len][cmd][payload...][chk] */
        out_buf[1] = (uint8_t)(data_len + 2);  /* len = cmd(1) + payload + chk(1) */
        if (data_len > 0) {
            memcpy(out_buf + 3, data, data_len);
        }
        chk_pos = (uint8_t)(3 + data_len);
        out_buf[chk_pos] = xlab_proto_calc_checksum(out_buf, (uint8_t)(chk_pos + 1));
        *out_len = (uint8_t)(chk_pos + 1);
    }
    else {
        /* 错误响应: [0xF4][0x03][cmd][err][chk] */
        out_buf[1] = 3;
        out_buf[3] = (uint8_t)err_code;
        out_buf[4] = xlab_proto_calc_checksum(out_buf, 5);
        *out_len = 5;
    }
#ifdef XLAB_DEBUG_TRACE
    xlab_print_hex("RESP_FRAME", out_buf, *out_len);
#endif
    return 0;
}

int xlab_router_process(
    xlab_packte_cmd_e cmd_code,
    const uint8_t* payload,
    uint8_t p_len,
    uint32_t seq,
    uint8_t* resp_buf,
    uint8_t* resp_len)
{
    if (p_len > XLAB_PROTO_PAYLOAD_MAX) {
        return XLAB_ERR_LEN_OVERFLOW;
    }

    xlab_router_entry_t* entry = xlab_router_lookup(cmd_code);
    if (!entry) {
        xlab_warn("Unknown command: 0x%02X", cmd_code);
        return XLAB_ERR_UNKNOWN_CMD;
    }

    if (p_len < entry->min_params || p_len > entry->max_params) {
        xlab_warn("Param length error for cmd 0x%02X (got %d, expect %d-%d)",
            cmd_code, p_len, entry->min_params, entry->max_params);
        return XLAB_ERR_PARAM_RANGE;
    }

    /* 执行具体业务 */
    uint8_t tmp_resp[64];
    uint8_t tmp_len = 0;
    int ret = entry->handler(payload, p_len, tmp_resp, &tmp_len);

    /* 映射错误码: 负值转为正错误码, 0转为OK */
    xlab_err_code_e err = (ret < 0) ? (xlab_err_code_e)(-ret) : XLAB_ERR_OK;

    return xlab_router_build_response(cmd_code, err, tmp_resp, tmp_len, seq, resp_buf, resp_len);
}