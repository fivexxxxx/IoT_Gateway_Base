#include "xlab_proto_parser.h"
#include <string.h>

xlab_parse_result_e xlab_proto_parse_byte(
    xlab_proto_ctx_t* ctx,
    uint8_t byte,
    xlab_packte_cmd_e* out_cmd,
    uint8_t* out_payload,
    uint8_t* out_len,
    uint32_t* out_seq)
{
    if (!ctx || !out_cmd || !out_len || !out_seq) {
        return XLAB_PARSE_ERROR;
    }

    switch (ctx->state) {
    case XLAB_PARSE_WAIT_HEAD:
        if (byte == XLAB_BOOT_CODE_HEAD) {
            ctx->frame_buf[ctx->buf_pos++] = byte;
            ctx->state = XLAB_PARSE_WAIT_LEN;
        }
        else {
            /* 关键：非 0x40 直接丢弃，不占缓冲，实现自动字节流重同步 */
            /* 例如：错位吞入的 02 02 BC 会被此逻辑逐个过滤，直到遇见下一个 40 */
        }
        break;

    case XLAB_PARSE_WAIT_LEN:
        ctx->frame_buf[1] = byte;
        if (byte < 2) { /* 最小有效帧: 命令码(1)+校验和(1) */
            ctx->state = XLAB_PARSE_WAIT_HEAD;
            return XLAB_PARSE_ERROR;
        }
        ctx->payload_len = (uint8_t)(byte - 2); /* 减去校验和字节 */
        if (ctx->payload_len > XLAB_PROTO_PAYLOAD_MAX) {
            ctx->state = XLAB_PARSE_WAIT_HEAD;
            return XLAB_PARSE_ERROR;
        }
        ctx->expected_total = byte + 2; /* 引导(1) + 长度字节(1) + 数据区(byte) */
        ctx->buf_pos = 2;
        ctx->state = XLAB_PARSE_WAIT_CMD;
        break;

    case XLAB_PARSE_WAIT_CMD:
        ctx->frame_buf[2] = byte;
        ctx->buf_pos = 3;
        ctx->state = (ctx->payload_len > 0) ? XLAB_PARSE_WAIT_PAYLOAD : XLAB_PARSE_WAIT_CHECK;
        break;

    case XLAB_PARSE_WAIT_PAYLOAD:
        ctx->frame_buf[ctx->buf_pos++] = byte;
        if (ctx->buf_pos >= (3 + ctx->payload_len)) {
            ctx->state = XLAB_PARSE_WAIT_CHECK;
        }
        break;

    case XLAB_PARSE_WAIT_CHECK:
        ctx->frame_buf[ctx->buf_pos++] = byte;
        /* 打印原始帧与校验和计算 */
#ifdef XLAB_DEBUG_TRACE
        xlab_print_hex("RECV_FRAME", ctx->frame_buf, ctx->expected_total);
        uint8_t calc_chk = xlab_proto_calc_checksum(ctx->frame_buf, ctx->expected_total);
        xlab_dbg("Checksum: calc=0x%02X, recv=0x%02X", calc_chk, byte);
#endif
        /* 校验和比对: 计算前 expected_total-1 字节之和 */
        if (xlab_proto_calc_checksum(ctx->frame_buf, ctx->expected_total) == byte) {
            /* 解析成功: 填充输出 */
            *out_cmd = (xlab_packte_cmd_e)ctx->frame_buf[2];
            *out_len = ctx->payload_len;
            if (out_payload && *out_len > 0) {
                memcpy(out_payload, &ctx->frame_buf[3], *out_len);
            }
            *out_seq = ctx->next_req_seq++; /* 分配并递增序列号 */

            /* 重置状态，准备解析粘包中的下一帧 */
            ctx->state = XLAB_PARSE_WAIT_HEAD;
            ctx->buf_pos = 0;
#ifdef XLAB_DEBUG_TRACE
            xlab_dbg("Parse OK: cmd=0x%02X, seq=%u, payload_len=%d",
                *out_cmd, *out_seq, *out_len);
#endif
            return XLAB_PARSE_OK;
        }
        else {
#ifdef XLAB_DEBUG_TRACE
            xlab_dbg("Parse CHK_ERR: cmd=0x%02X (fallback to NONE)",
                (uint8_t)ctx->frame_buf[2]);
#endif
            /* 校验失败: 重置状态，返回可恢复错误 */
            ctx->state = XLAB_PARSE_WAIT_HEAD;
            ctx->buf_pos = 0;
            return XLAB_PARSE_CHK_ERR;
        }
    }

    return XLAB_PARSE_INCOMPLETE;
}