#include "ws_frame.h"

ws_frame_status ws_parse_frame(const uint8_t *buf, size_t len, size_t max_frame, ws_frame *f) {
    if (len < 2) return WS_FRAME_INCOMPLETE;

    f->fin = buf[0] >> 7;
    f->opcode = buf[0] & 0x0F;
    f->masked = buf[1] >> 7;

    size_t header_len = 2;
    uint64_t payload_len = buf[1] & 0x7F;
    if (payload_len == 126) {
        header_len += 2;
        if (len < header_len) return WS_FRAME_INCOMPLETE;
        payload_len = (uint64_t)buf[2] << 8 | buf[3];
    } else if (payload_len == 127) {
        header_len += 8;
        if (len < header_len) return WS_FRAME_INCOMPLETE;
        payload_len = 0;
        for (int i = 0; i < 8; i++) payload_len = payload_len << 8 | buf[2 + i];
    }
    if (f->masked) header_len += 4;

    if (payload_len > max_frame || header_len + payload_len > max_frame) return WS_FRAME_TOO_BIG;
    if (len < header_len) return WS_FRAME_INCOMPLETE;

    if (f->masked) {
        for (int i = 0; i < 4; i++) f->mask[i] = buf[header_len - 4 + i];
    }
    f->header_len = header_len;
    f->payload_len = payload_len;

    return len < header_len + payload_len ? WS_FRAME_INCOMPLETE : WS_FRAME_OK;
}

void ws_unmask(uint8_t *payload, size_t len, const uint8_t mask[4]) {
    for (size_t i = 0; i < len; i++) payload[i] ^= mask[i % 4];
}

size_t ws_frame_header(uint8_t out[4], uint8_t opcode, size_t len) {
    out[0] = 0x80 | (opcode & 0x0F);    /* FIN */
    if (len <= 125) {
        out[1] = len;
        return 2;
    }
    if (len <= 65535) {
        out[1] = 126;
        out[2] = (len >> 8) & 0xFF;
        out[3] = len & 0xFF;
        return 4;
    }
    return 0;
}
