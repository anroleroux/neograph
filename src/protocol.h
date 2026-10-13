#ifndef PROTOCOL_H_INCLUDED
#define PROTOCOL_H_INCLUDED

/*
 * Protocol constants. PROTOCOL.md describes how they are used.
 *
 * The PKT_, MSG_, FIELD_, NET_OPT_, PWM_, SLIDER_, VALUE_, PIN_ and ADC_ constants are mirrored in
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

#define PKT_CONFIG_MAX          4096    /* largest 0A packet, header included: the flash config region */

/* ── Messages (the body of a packet) ──────────────────────────────────── */

#define MSG_HEADER_LEN          5       /* type (1), reserved (1), element ID (2, BE), payload length (1) */

/* Control element types (0x00-0x3F): bit 5 set = HW, bit 4 set = input. */
#define MSG_HW_BIT              0x20
#define MSG_INPUT_BIT           0x10

#define MSG_UI_GAUGE            0x00    /* reserved */
#define MSG_UI_INDICATOR        0x01    /* reserved */
#define MSG_UI_GRAPH            0x02    /* a plot, drawn in the browser */
#define MSG_UI_BUTTON           0x10
#define MSG_UI_SWITCH           0x11    /* reserved */
#define MSG_UI_SLIDER           0x12
#define MSG_UI_SLIDER_2D        0x13    /* reserved */
#define MSG_UI_MULTI_SLIDER     0x14    /* reserved */
#define MSG_UI_DIAL             0x15    /* reserved */
#define MSG_UI_TIMER            0x16    /* reserved */
#define MSG_HW_LED              0x20
#define MSG_HW_GPO              0x21
#define MSG_HW_PWM              0x22
#define MSG_HW_CORE             0x30    /* the core temperature, from telemetry */
#define MSG_HW_GPI              0x31    /* reserved */
#define MSG_HW_ADC              0x32    /* an analog input pin */

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

/* Command values: one byte, a fraction of full scale, 0x00 = 0 to 0xFF = 1. */
#define VALUE_FULL              0xFF    /* 1: a pressed button, a slider at its end */
#define VALUE_HALF              0x80    /* on/off outputs (LED, GPO) are on from here up */

/* Slider (MSG_UI_SLIDER) payload byte 0, bit 0: 0 = horizontal, 1 = vertical. */
#define SLIDER_VERTICAL         0x01

/* GPIO pins a GPO or PWM may use, bit n = GPIO n: GPIO 0-22 and 26-28, the ones on
   the Pico 2 W's header. GPIO 23, 24, 25 and 29 drive the WiFi chip. */
#define PIN_USABLE_MASK         0x1C7FFFFF

/* ADC input (MSG_HW_ADC) payload byte 0: the channel, 0 to ADC_CHANNELS - 1, on GPIO
   ADC_PIN_FIRST + channel. ADC3, on GPIO 29, is the WiFi chip's clock on the Pico 2 W. */
#define ADC_CHANNELS            3
#define ADC_PIN_FIRST           26
#define ADC_RAW_FULL            4095    /* a 12-bit reading at the reference voltage, 3.3 V */

/* PWM output (MSG_HW_PWM) payload limits and options. */
#define PWM_FREQ_MIN            10      /* Hz; the slowest the PWM counter can run at 150 MHz is ~9 Hz */
#define PWM_DUTY_MAX            100     /* duty cycle in percent */
#define PWM_DUTY_INPUT          0x01    /* options bit 0: 0 = constant duty, 1 = duty from its input, min to max */
#define PWM_FREQ_INPUT          0x02    /* options bit 1: 0 = constant frequency, 1 = frequency from its input, min to max */
#define PWM_FREQ_LOG            0x04    /* options bit 2: a frequency from its input goes from min to max 0 = linearly, 1 = logarithmically */

static inline int pin_usable(uint8_t pin) { return pin < 32 && (PIN_USABLE_MASK >> pin & 1); }

/* ── Inter-core FIFO (core 0 <-> core 1) ──────────────────────────────── */

/* Core 0 -> core 1: operation word, bits 31-30. */
#define FIFO_OP_CONTROL_ACTION  1
#define FIFO_OP_SETUP           2
#define FIFO_OP_CLEAN_SETUP     3       /* sent first for every config, clears core 1's controls */

/* Core 1 -> core 0: tag word, followed by one value word. */
#define FIFO_TAG_UPTIME         0       /* value: uptime in 1.2 s ticks */
#define FIFO_TAG_TEMPERATURE    1       /* value: float bits, degrees C */
#define FIFO_TAG_LED            2       /* value: onboard LED state, 0 or 1 */
#define FIFO_TAG_ADC            3       /* value: an ADC reading, see fifo_adc_word */

/*
 * Operation word: [31:30] op · [29] unused · [28:24] type & 0x1F ·
 * [23:8] element ID · [7:0] value (action), or GPIO pin or ADC channel (setup)
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

/* ADC word, the value after FIFO_TAG_ADC: [31:16] channel · [15:0] reading, 0 to ADC_RAW_FULL. */
static inline uint32_t fifo_adc_word(uint8_t channel, uint16_t raw) { return (uint32_t)channel << 16 | raw; }
static inline uint8_t fifo_adc_channel(uint32_t word) { return (word >> 16) & 0xFF; }
static inline uint16_t fifo_adc_raw(uint32_t word) { return word & 0xFFFF; }

/* Reference word, sent after a setup word for LED/GPO/PWM: [31:16] source element ID. */
static inline uint32_t fifo_ref_word(uint16_t source_id) { return (uint32_t)source_id << 16; }
static inline uint16_t fifo_ref_source(uint32_t word) { return (word >> 16) & 0xFFFF; }

/* PWM word, sent after the reference word for PWM: [31:16] frequency in Hz · [7:0] options (PWM_DUTY_INPUT). */
static inline uint32_t fifo_pwm_word(uint16_t freq, uint8_t options) { return (uint32_t)freq << 16 | options; }
static inline uint16_t fifo_pwm_freq(uint32_t word) { return (word >> 16) & 0xFFFF; }
static inline uint8_t fifo_pwm_options(uint32_t word) { return word & 0xFF; }

/* Duty word, sent after the PWM word: [31:24] constant duty · [23:16] min duty · [15:8] max duty, in percent. */
static inline uint32_t fifo_duty_word(uint8_t duty, uint8_t min, uint8_t max) {
    return (uint32_t)duty << 24 | (uint32_t)min << 16 | (uint32_t)max << 8;
}
static inline uint8_t fifo_duty_const(uint32_t word) { return (word >> 24) & 0xFF; }
static inline uint8_t fifo_duty_min(uint32_t word) { return (word >> 16) & 0xFF; }
static inline uint8_t fifo_duty_max(uint32_t word) { return (word >> 8) & 0xFF; }

/* Frequency word, sent after the frequency's reference word: [31:16] min Hz · [15:0] max Hz. */
static inline uint32_t fifo_freq_word(uint16_t min, uint16_t max) { return (uint32_t)min << 16 | max; }
static inline uint16_t fifo_freq_min(uint32_t word) { return (word >> 16) & 0xFFFF; }
static inline uint16_t fifo_freq_max(uint32_t word) { return word & 0xFFFF; }

#endif
