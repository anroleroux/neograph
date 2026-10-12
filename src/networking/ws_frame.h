#ifndef WS_FRAME_H_INCLUDED
#define WS_FRAME_H_INCLUDED

/*
 * WebSocket frame parsing and building (RFC 6455), without any lwIP dependency
 * so it can be unit tested on the host.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WS_MAX_HEADER_LEN 14    /* 2 + 8 (64-bit length) + 4 (mask) */

#define WS_OP_CONTINUATION 0x0
#define WS_OP_TEXT         0x1
#define WS_OP_BINARY       0x2
#define WS_OP_CLOSE        0x8
#define WS_OP_PING         0x9
#define WS_OP_PONG         0xA

typedef struct {
    bool fin;
    uint8_t opcode;
    bool masked;
    uint8_t mask[4];
    size_t header_len;
    uint64_t payload_len;
} ws_frame;

typedef enum {
    WS_FRAME_INCOMPLETE,    /* need more bytes */
    WS_FRAME_OK,            /* a whole frame is at the start of the buffer */
    WS_FRAME_TOO_BIG,       /* the frame can never fit in max_frame bytes */
} ws_frame_status;

/* Parse the frame at the start of buf (len bytes available). A frame bigger than
 * max_frame bytes, header included, is reported as soon as its length is known. */
ws_frame_status ws_parse_frame(const uint8_t *buf, size_t len, size_t max_frame, ws_frame *f);

/* Unmask a client frame's payload in place. */
void ws_unmask(uint8_t *payload, size_t len, const uint8_t mask[4]);

/* Write an unmasked, final server frame header for a payload of len bytes into out.
 * Returns the header length (2 or 4), or 0 if len is over 65535. */
size_t ws_frame_header(uint8_t out[4], uint8_t opcode, size_t len);

#endif
