#ifndef PROTOCOL_H_INCLUDED
#define PROTOCOL_H_INCLUDED

/*
 * Protocol constants. PROTOCOL.md describes how they are used.
 *
 * The PKT_, MSG_, FIELD_ and NET_OPT_ constants are mirrored in
 * ui/content/protocol.js; `make test` fails if the two disagree.
 * The FIFO_ constants and helpers are firmware-only (core 0 <-> core 1).
 */

#include <stdint.h>

/* ── Packets (UI <-> HW, one per WebSocket binary frame) ─────────────── */

#define PKT_HEADER_LEN          3       /* type (1), payload length (2, big endian) */

#define PKT_COMMAND             0x01    /* UI -> HW */
#define PKT_DATA_VALUE          0x02    /* reserved, not implemented */
#define PKT_CONFIG              0x0A    /* UI <-> HW, also the flash format */
#define PKT_UPDATE_CONFIG       0x0B    /* UI -> HW */
#define PKT_UPDATE_SECRET       0x0C    /* UI -> HW */
#define PKT_DEVICE_ID           0x0D    /* HW -> UI */

/* ── Messages (the body of a packet) ──────────────────────────────────── */

#define MSG_HEADER_LEN          5       /* type (1), reserved (1), element ID (2, BE), payload length (1) */

/* Control element types (0x00-0x3F): bit 5 set = HW, bit 4 set = input. */
#define MSG_HW_BIT              0x20
#define MSG_INPUT_BIT           0x10

#define MSG_UI_GAUGE            0x00    /* reserved */
#define MSG_UI_INDICATOR        0x01    /* reserved */
#define MSG_UI_GRAPH            0x02    /* reserved */
#define MSG_UI_BUTTON           0x10
#define MSG_UI_SWITCH           0x11    /* reserved */
#define MSG_UI_SLIDER           0x12    /* reserved */
#define MSG_UI_SLIDER_2D        0x13    /* reserved */
#define MSG_UI_MULTI_SLIDER     0x14    /* reserved */
#define MSG_UI_DIAL             0x15    /* reserved */
#define MSG_UI_TIMER            0x16    /* reserved */
#define MSG_HW_LED              0x20
#define MSG_HW_GPO              0x21
#define MSG_HW_PWM              0x22    /* reserved */
#define MSG_HW_CORE             0x30    /* reserved */
#define MSG_HW_GPI              0x31    /* reserved */
#define MSG_HW_ADC              0x32    /* reserved */

/* Supporting and networking element types. */
#define MSG_STRING              0xA0    /* a node's label, keyed by node ID */
#define MSG_LAYOUT              0xA1    /* a node's position, keyed by node ID */
#define MSG_WIRE                0xA2    /* the shape of an output's wire, keyed by output ID */
#define MSG_DEVICE              0xB0
#define MSG_DEVICE_FIELD        0xB1
#define MSG_NETWORK             0xB2
#define MSG_NETWORK_FIELD       0xB3

/* Field IDs: the first payload byte of a 0xB0-0xB3 message. */
#define FIELD_DEVICE_DID        0x00
#define FIELD_DEVICE_NAME       0x01
#define FIELD_DEVICE_NETWORK    0x02
#define FIELD_NETWORK_OPTIONS   0x00
#define FIELD_NETWORK_NAME      0x01
#define FIELD_NETWORK_SSID      0x02
#define FIELD_NETWORK_PASSWORD  0x03    /* secrets store only */

/* Network options (FIELD_NETWORK_OPTIONS) bit 0: 0 = start an access point, 1 = join a network. */
#define NET_OPT_JOIN            0x01

/* ── Inter-core FIFO (core 0 <-> core 1) ──────────────────────────────── */

/* Core 0 -> core 1: operation word, bits 31-30. */
#define FIFO_OP_CONTROL_ACTION  1
#define FIFO_OP_SETUP           2
#define FIFO_OP_CLEAN_SETUP     3       /* handled by core 1, never sent */

/* Core 1 -> core 0: tag word, followed by one value word. */
#define FIFO_TAG_UPTIME         0       /* value: uptime in 1.2 s ticks */
#define FIFO_TAG_TEMPERATURE    1       /* value: float bits, degrees C */
#define FIFO_TAG_LED            2       /* value: onboard LED state, 0 or 1 */

/*
 * Operation word: [31:30] op · [29] unused · [28:24] type & 0x1F ·
 * [23:8] element ID · [7:0] value (action) or GPIO pin (setup)
 */
static inline uint32_t fifo_op_word(uint8_t op, uint8_t type, uint16_t id, uint8_t value) {
    return (uint32_t)(op & 0x03) << 30
        | (uint32_t)(type & 0x1F) << 24
        | (uint32_t)id << 8
        | value;
}

static inline uint8_t fifo_op(uint32_t word) { return (word >> 30) & 0x03; }
/* Only 5 type bits travel, and bit 5 is forced on: UI types arrive as HW types. */
static inline uint8_t fifo_op_type(uint32_t word) { return ((word >> 24) & 0x1F) | 0x20; }
static inline uint16_t fifo_op_id(uint32_t word) { return (word >> 8) & 0xFFFF; }
static inline uint8_t fifo_op_value(uint32_t word) { return word & 0xFF; }

/* Reference word, sent after a setup word for LED/GPO: [31:16] source element ID. */
static inline uint32_t fifo_ref_word(uint16_t source_id) { return (uint32_t)source_id << 16; }
static inline uint16_t fifo_ref_source(uint32_t word) { return (word >> 16) & 0xFFFF; }

#endif
