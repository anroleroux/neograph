#ifndef PROTOCOL_H_INCLUDED
#define PROTOCOL_H_INCLUDED

/*
 * Protocol constants. PROTOCOL.md describes how they are used.
 *
 * The PKT_, MSG_, FIELD_, NET_OPT_, NET_LEASE_, PWM_, SLIDER_, BUTTON_, VALUE_, PIN_, ADC_, GPI_,
 * KEYBOARD_, CORE_, LOGIC_, COMPARE_, DELAY_ and SCALE_ constants are mirrored in
 * ui/content/protocol.js; `make test` fails if the two disagree.
 * The FIFO_ constants and helpers are firmware-only (core 0 <-> core 1).
 */

#include <stdint.h>
#include <string.h>

/* ── Packets (UI <-> HW, one per WebSocket binary frame) ─────────────── */

#define PKT_HEADER_LEN          3       /* type (1), payload length (2, big endian) */

#define PKT_COMMAND             0x01    /* UI -> HW */
#define PKT_DATA_VALUE          0x02    /* reserved, not implemented */
#define PKT_CONFIG              0x0A    /* UI <-> HW, also the flash format */
#define PKT_UPDATE_CONFIG       0x0B    /* UI -> HW */
#define PKT_UPDATE_SECRET       0x0C    /* UI -> HW */
#define PKT_DEVICE_ID           0x0D    /* HW -> UI */

#define PKT_CONFIG_MAX          4096    /* largest 0A packet, header included: the flash config region */

/* ── Messages (the body of a packet) ──────────────────────────────────── */

#define MSG_HEADER_LEN          5       /* type (1), flags (1), element ID (2, BE), payload length (1) */

/* Message header byte 1, bit 0: each source reference in the payload is 3 bytes, the
   element ID then the index of its output (MSG_FLAG_OUTPUT). Without it, a reference
   is the 2-byte element ID, and means the element's output 0: messages saved before
   sources had several outputs. */
#define MSG_FLAG_OUTPUT         0x01

/* Message header byte 1, bit 1: a hardware input (core temperature, ADC, GPIO input) or
   a logic element shows a tile in the Command view (MSG_FLAG_SHOWN). The device ignores it. */
#define MSG_FLAG_SHOWN          0x02

/* Control element types (0x00-0x7F): bit 6 set = logic (below); otherwise bit 5 set =
   HW, bit 4 set = input. */
#define MSG_HW_BIT              0x20
#define MSG_INPUT_BIT           0x10

#define MSG_UI_GAUGE            0x00    /* reserved */
#define MSG_UI_INDICATOR        0x01    /* a light, drawn in the browser */
#define MSG_UI_GRAPH            0x02    /* a plot, drawn in the browser */
#define MSG_UI_BUTTON           0x10
#define MSG_UI_SWITCH           0x11    /* reserved */
#define MSG_UI_SLIDER           0x12
#define MSG_UI_SLIDER_2D        0x13    /* two outputs: X (0) and Y (1) */
#define MSG_UI_MULTI_SLIDER     0x14    /* reserved */
#define MSG_UI_DIAL             0x15    /* reserved */
#define MSG_UI_TIMER            0x16    /* reserved */
#define MSG_HW_LED              0x20
#define MSG_HW_GPO              0x21
#define MSG_HW_PWM              0x22
#define MSG_HW_KEYBOARD         0x23    /* a keystroke typed to the PC over USB */
#define MSG_HW_CORE             0x30    /* the chip's temperature */
#define MSG_HW_GPI              0x31    /* a GPIO input pin */
#define MSG_HW_ADC              0x32    /* an analog input pin */

/* Logic element types (0x40-0x7F): run on the device. Each reads sources and is a
   source itself, with one output, 0: boolean, or floating for a scale, min or max. */
#define MSG_LOGIC_BIT           0x40
#define MSG_LOGIC_COMPARE       0x40    /* a floating input above or below a threshold */
#define MSG_LOGIC_AND           0x41    /* on while every input is on */
#define MSG_LOGIC_OR            0x42    /* on while any input is on */
#define MSG_LOGIC_NOT           0x43    /* on while its input is off */
#define MSG_LOGIC_DELAY         0x44    /* its input, after an on delay and an off delay */
#define MSG_LOGIC_SCALE         0x45    /* floating: its input mapped from one range onto another */
#define MSG_LOGIC_MIN           0x46    /* floating: the least of its inputs */
#define MSG_LOGIC_MAX           0x47    /* floating: the greatest of its inputs */
#define MSG_LOGIC_PULSE         0x48    /* on for a time after its input goes on */
#define MSG_LOGIC_BLINK         0x49    /* on and off in turn while its input is on */
#define MSG_LOGIC_TOGGLE        0x4A    /* turns over each time its input goes on */
#define MSG_LOGIC_LATCH         0x4B    /* on once set, off once reset; reset wins */
#define MSG_LOGIC_LAST          0x4B    /* the last logic type: MSG_LOGIC_LATCH */

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
#define FIELD_NETWORK_AP_ADDRESS     0x04    /* access point: its IPv4 address, 4 bytes */
#define FIELD_NETWORK_STATIC_ADDRESS 0x05    /* joining: a fixed IPv4 address, 4 bytes */
#define FIELD_NETWORK_GATEWAY        0x06    /* joining with a fixed address: the gateway, 4 bytes */

/* Network options (FIELD_NETWORK_OPTIONS) bit 0: 0 = start an access point, 1 = join a network. */
#define NET_OPT_JOIN            0x01

/* IPv4 address fields: 4 bytes, most significant first; 0.0.0.0 = not set. The access
   point's network is its address's /24, and its DHCP server leases the
   NET_LEASE_COUNT addresses from host NET_LEASE_FIRST, so the access point may not
   take one of those. */
#define NET_LEASE_FIRST         16
#define NET_LEASE_COUNT         8

/* Whether an address field's value is one the device uses: a unicast address that
   isn't loopback, the network (.0) or the broadcast (.255) of its /24. */
static inline int net_address_usable(const uint8_t a[4]) {
    return a[0] != 0 && a[0] != 127 && a[0] < 224 && a[3] != 0 && a[3] != 255;
}

/* Whether an access point may take an address: usable, and not one it leases. */
static inline int net_ap_address_usable(const uint8_t a[4]) {
    return net_address_usable(a) && (a[3] < NET_LEASE_FIRST || a[3] >= NET_LEASE_FIRST + NET_LEASE_COUNT);
}

/* Command values: one byte, a fraction of full scale, 0x00 = 0 to 0xFF = 1. */
#define VALUE_FULL              0xFF    /* 1: a pressed button, a slider at its end */
#define VALUE_HALF              0x80    /* on/off outputs (LED, GPO) are on from here up */

/* Slider (MSG_UI_SLIDER) payload byte 0, bit 0: 0 = horizontal, 1 = vertical. */
#define SLIDER_VERTICAL         0x01

/* 2D slider (MSG_UI_SLIDER_2D) payload byte 0, bit 0: 0 = it stays where it is let go,
   1 = it returns to the centre (VALUE_HALF on both outputs). Bits 1-2: how it returns.
   Byte 1: its return speed, 1-255 % of its range per second. Bytes 2-3: its home, X then
   Y as values (0 to VALUE_FULL): where it starts, and where it returns to. */
#define SLIDER_RETURN           0x01
#define SLIDER_EASE_MASK        0x06
#define SLIDER_EASE_IMMEDIATE   0x00    /* at once */
#define SLIDER_EASE_LINEAR      0x02    /* at a steady speed */
#define SLIDER_EASE_SIGMOID     0x04    /* speeding up, then slowing down (smoothstep) */

/* Button (MSG_UI_BUTTON) payload byte 0, bit 0: 0 = each click toggles it, 1 = it is on
   while held. A button without the byte is a toggle. */
#define BUTTON_MOMENTARY        0x01

/* GPIO pins a GPO or PWM may use, bit n = GPIO n: GPIO 0-22 and 26-28, the ones on
   the Pico 2 W's header. GPIO 23, 24, 25 and 29 drive the WiFi chip. */
#define PIN_USABLE_MASK         0x1C7FFFFF

/* ADC input (MSG_HW_ADC) payload byte 0: the channel, 0 to ADC_CHANNELS - 1, on GPIO
   ADC_PIN_FIRST + channel. ADC3, on GPIO 29, is the WiFi chip's clock on the Pico 2 W. */
#define ADC_CHANNELS            3
#define ADC_PIN_FIRST           26
#define ADC_RAW_FULL            4095    /* a 12-bit reading at the reference voltage, 3.3 V */

/* Core temperature (MSG_HW_CORE): on the device it drives outputs with its value from
   0 °C (0) to CORE_TEMP_FULL °C (VALUE_FULL), clamped. */
#define CORE_TEMP_FULL          100

/* Logic elements. A compare (MSG_LOGIC_COMPARE): a source reference, then its condition
   (COMPARE_ABOVE or COMPARE_BELOW), its threshold and its hysteresis, each a 32-bit
   float, big endian. An AND or OR: 1 to LOGIC_INPUTS_MAX source references. A NOT: one.
   A delay (MSG_LOGIC_DELAY): a source reference, then its on delay and its off delay,
   each a 32-bit big-endian number of milliseconds, 0 to DELAY_MAX_MS.
   A scale (MSG_LOGIC_SCALE): a source reference, its options (SCALE_CLAMP), then four
   32-bit big-endian floats: the input's from and to, the output's from and to.
   A min or max: 1 to LOGIC_INPUTS_MAX source references. A pulse: a source reference,
   then its time on, 1 to DELAY_MAX_MS. A blink: a source reference (0: it always
   blinks), then its time on and time off, each 1 to DELAY_MAX_MS. A toggle: a source
   reference. A latch: its set source reference, then its reset one. */
#define COMPARE_ABOVE           0x00    /* on above the threshold; off below it less the hysteresis */
#define COMPARE_BELOW           0x01    /* on below the threshold; off above it plus the hysteresis */
#define LOGIC_INPUTS_MAX        4
#define DELAY_MAX_MS            86400000    /* a day */
#define SCALE_CLAMP             0x01    /* options bit 0: keep the output within its range */

/* GPIO input (MSG_HW_GPI) payload byte 0: the GPIO pin, a usable one (pin_usable). Byte 1:
   options. Bits 0-1: the pin's pull (GPI_PULL_). Bit 2 (GPI_INVERT): 0 = on while the pin
   is high, 1 = on while it is low. */
#define GPI_PULL_MASK           0x03
#define GPI_PULL_NONE           0x00
#define GPI_PULL_UP             0x01
#define GPI_PULL_DOWN           0x02
#define GPI_INVERT              0x04

/* USB keyboard (MSG_HW_KEYBOARD) payload byte 2: what it types each time its input goes on. */
#define KEYBOARD_COMBO          0x00    /* one key with modifiers: byte 3 the HID modifier bits, byte 4 the HID key */
#define KEYBOARD_TEXT           0x01    /* text: bytes 3… printable ASCII, \t or \n, typed on a US layout */
#define KEYBOARD_TEXT_MAX       64      /* characters in a text keystroke */

/* PWM output (MSG_HW_PWM) payload limits and options. */
#define PWM_FREQ_MIN            10      /* Hz; the slowest the PWM counter can run at 150 MHz is ~9 Hz */
#define PWM_DUTY_MAX            100     /* duty cycle in percent */
#define PWM_DUTY_INPUT          0x01    /* options bit 0: 0 = constant duty, 1 = duty from its input, min to max */
#define PWM_FREQ_INPUT          0x02    /* options bit 1: 0 = constant frequency, 1 = frequency from its input, min to max */
#define PWM_FREQ_LOG            0x04    /* options bit 2: a frequency from its input goes from min to max 0 = linearly, 1 = logarithmically */

static inline int pin_usable(uint8_t pin) { return pin < 32 && (PIN_USABLE_MASK >> pin & 1); }

/* A source reference: an output of a source element. ID 0 is "not connected". */
typedef struct {
    uint16_t id;
    uint8_t output;
} msg_ref;

/* The length of a source reference in a message with these header flags. */
static inline uint8_t msg_ref_len(uint8_t flags) { return flags & MSG_FLAG_OUTPUT ? 3 : 2; }

/* The source reference at p, in a message with these header flags. */
static inline msg_ref msg_ref_at(const uint8_t *p, uint8_t flags) {
    msg_ref ref = { (uint16_t)(p[0] << 8 | p[1]), 0 };
    if (flags & MSG_FLAG_OUTPUT) ref.output = p[2];
    return ref;
}

/* Whether a USB keyboard's payload is one the device types: a source reference, mode,
   then a key with its modifiers, or 1 to KEYBOARD_TEXT_MAX characters it can type. */
static inline int keyboard_payload_valid(const uint8_t *pay, uint8_t len, uint8_t flags) {
    uint8_t r = msg_ref_len(flags);
    if (len < r + 1) return 0;
    const uint8_t *k = pay + r;     /* the keystroke: mode, then its key or text */
    len -= r;
    if (k[0] == KEYBOARD_COMBO) return len == 3;
    if (k[0] != KEYBOARD_TEXT || len < 2 || len > 1 + KEYBOARD_TEXT_MAX) return 0;
    for (uint8_t i = 1; i < len; i++) {
        if ((k[i] < 0x20 || k[i] > 0x7E) && k[i] != '\t' && k[i] != '\n') return 0;
    }
    return 1;
}

/* A 2D slider's home, X then Y as values: payload bytes 2-3, or without them the centre. */
static inline void slider_2d_home(const uint8_t *pay, uint8_t len, uint8_t home[2]) {
    home[0] = len >= 4 ? pay[2] : VALUE_HALF;
    home[1] = len >= 4 ? pay[3] : VALUE_HALF;
}

/* A PWM (MSG_HW_PWM) payload, read by pwm_payload_read. */
typedef struct {
    msg_ref duty_source;
    uint8_t pin;
    uint16_t freq;
    uint8_t options;
    uint8_t duty, duty_min, duty_max;
    msg_ref freq_source;
    uint16_t freq_min, freq_max;
} pwm_payload;

/* The payload length of a PWM message with these header flags: 15, or 17 with outputs. */
static inline uint8_t pwm_payload_len(uint8_t flags) { return 11 + 2 * msg_ref_len(flags); }

/* Read a PWM payload. Returns 0 if it is not the length a PWM's is. */
static inline int pwm_payload_read(const uint8_t *pay, uint8_t len, uint8_t flags, pwm_payload *out) {
    if (len != pwm_payload_len(flags)) return 0;
    uint8_t r = msg_ref_len(flags);
    const uint8_t *q = pay + r;     /* after the duty source */
    const uint8_t *f = q + 7 + r;   /* after the frequency source */
    out->duty_source = msg_ref_at(pay, flags);
    out->pin = q[0];
    out->freq = q[1] << 8 | q[2];
    out->options = q[3];
    out->duty = q[4];
    out->duty_min = q[5];
    out->duty_max = q[6];
    out->freq_source = msg_ref_at(q + 7, flags);
    out->freq_min = f[0] << 8 | f[1];
    out->freq_max = f[2] << 8 | f[3];
    return 1;
}

/* A 32-bit big-endian number at p. */
static inline uint32_t msg_u32_at(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* A 32-bit big-endian IEEE 754 float at p. */
static inline float msg_float_at(const uint8_t *p) {
    uint32_t u = msg_u32_at(p);
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}

/* Whether a logic element's payload is one the device runs: its source references
   (one; for an AND, OR, min or max 1 to LOGIC_INPUTS_MAX; a latch's two), then a
   compare's condition, threshold and hysteresis (finite, the hysteresis 0 or more), a
   delay's, pulse's or blink's times, or a scale's options and ranges (finite, its
   input's from and to apart). */
static inline int logic_payload_valid(uint8_t type, const uint8_t *pay, uint8_t len, uint8_t flags) {
    uint8_t r = msg_ref_len(flags);
    switch (type) {
    case MSG_LOGIC_COMPARE: {
        if (len != r + 9 || pay[r] > COMPARE_BELOW) return 0;
        float threshold = msg_float_at(pay + r + 1), hysteresis = msg_float_at(pay + r + 5);
        /* finite: x - x is 0 for a number, NaN for an infinity or NaN */
        return threshold - threshold == 0 && hysteresis - hysteresis == 0 && hysteresis >= 0;
    }
    case MSG_LOGIC_AND:
    case MSG_LOGIC_OR:
    case MSG_LOGIC_MIN:
    case MSG_LOGIC_MAX:
        return len >= r && len <= r * LOGIC_INPUTS_MAX && len % r == 0;
    case MSG_LOGIC_NOT:
    case MSG_LOGIC_TOGGLE:
        return len == r;
    case MSG_LOGIC_LATCH:
        return len == 2 * r;
    case MSG_LOGIC_DELAY:
        return len == r + 8 && msg_u32_at(pay + r) <= DELAY_MAX_MS && msg_u32_at(pay + r + 4) <= DELAY_MAX_MS;
    case MSG_LOGIC_PULSE:
        return len == r + 4 && msg_u32_at(pay + r) >= 1 && msg_u32_at(pay + r) <= DELAY_MAX_MS;
    case MSG_LOGIC_BLINK:
        return len == r + 8 && msg_u32_at(pay + r) >= 1 && msg_u32_at(pay + r) <= DELAY_MAX_MS
            && msg_u32_at(pay + r + 4) >= 1 && msg_u32_at(pay + r + 4) <= DELAY_MAX_MS;
    case MSG_LOGIC_SCALE: {
        if (len != r + 17 || (pay[r] & ~SCALE_CLAMP)) return 0;
        float k[4];
        for (int i = 0; i < 4; i++) {
            k[i] = msg_float_at(pay + r + 1 + 4 * i);
            if (k[i] - k[i] != 0) return 0;     /* not finite */
        }
        return k[0] != k[1];                    /* an input range to map from */
    }
    default:
        return 0;
    }
}

/* ── Inter-core FIFO (core 0 <-> core 1) ──────────────────────────────── */

/* Core 0 -> core 1: operation word, bits 31-30. Core 0 sends only action words: the
   config reaches core 1 as a program in shared RAM (src/program.h). */
#define FIFO_OP_CONTROL_ACTION  1

/* Core 1 -> core 0: tag word, followed by one value word. Tags 1, 3 and 5 are
   retired: the temperature, ADC and GPIO input readings are signals, which core 0
   reads from the program. */
#define FIFO_TAG_UPTIME         0       /* value: uptime in 1.2 s ticks */
#define FIFO_TAG_LED            2       /* value: onboard LED state, 0 or 1 */
#define FIFO_TAG_KEYBOARD       4       /* value: the ID of a USB keyboard whose input went on */

/* An action word names one of an element's first FIFO_OUTPUTS_MAX outputs. */
#define FIFO_OUTPUTS_MAX        32

/*
 * Action word: the new value of one output of a source, from a command.
 * [31:30] op · [29] unused · [28:24] output index · [23:8] element ID · [7:0] value
 */
static inline uint32_t fifo_action_word(uint16_t id, uint8_t output, uint8_t value) {
    return (uint32_t)FIFO_OP_CONTROL_ACTION << 30
        | (uint32_t)(output & 0x1F) << 24
        | (uint32_t)id << 8
        | value;
}

static inline uint8_t fifo_op(uint32_t word) { return (word >> 30) & 0x03; }
static inline uint8_t fifo_op_output(uint32_t word) { return (word >> 24) & 0x1F; }
static inline uint16_t fifo_op_id(uint32_t word) { return (word >> 8) & 0xFFFF; }
static inline uint8_t fifo_op_value(uint32_t word) { return word & 0xFF; }

#endif
