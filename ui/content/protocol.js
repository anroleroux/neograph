// Protocol constants, mirrored from src/protocol.h (`make test` checks that the
// two agree). PROTOCOL.md describes how they are used.

// ── Packets (UI <-> HW, one per WebSocket binary frame)
const PKT_HEADER_LEN         = 3;     // type (1), payload length (2, big endian)

const PKT_COMMAND            = 0x01;  // UI -> HW
const PKT_DATA_VALUE         = 0x02;  // reserved, not implemented
const PKT_CONFIG             = 0x0A;  // UI <-> HW, also the flash format
const PKT_UPDATE_CONFIG      = 0x0B;  // UI -> HW
const PKT_UPDATE_SECRET      = 0x0C;  // UI -> HW
const PKT_DEVICE_ID          = 0x0D;  // HW -> UI

const PKT_CONFIG_MAX         = 4096;  // largest 0A packet, header included: the flash config region

// ── Messages (the body of a packet)
const MSG_HEADER_LEN         = 5;     // type (1), reserved (1), element ID (2, BE), payload length (1)

// Control element types (0x00-0x3F): bit 5 set = HW, bit 4 set = input.
const MSG_HW_BIT             = 0x20;
const MSG_INPUT_BIT          = 0x10;

const MSG_UI_GAUGE           = 0x00;  // reserved
const MSG_UI_INDICATOR       = 0x01;  // reserved
const MSG_UI_GRAPH           = 0x02;  // a plot, drawn in the browser
const MSG_UI_BUTTON          = 0x10;
const MSG_UI_SWITCH          = 0x11;  // reserved
const MSG_UI_SLIDER          = 0x12;
const MSG_UI_SLIDER_2D       = 0x13;  // reserved
const MSG_UI_MULTI_SLIDER    = 0x14;  // reserved
const MSG_UI_DIAL            = 0x15;  // reserved
const MSG_UI_TIMER           = 0x16;  // reserved
const MSG_HW_LED             = 0x20;
const MSG_HW_GPO             = 0x21;
const MSG_HW_PWM             = 0x22;
const MSG_HW_CORE            = 0x30;  // the core temperature, from telemetry
const MSG_HW_GPI             = 0x31;  // reserved
const MSG_HW_ADC             = 0x32;  // reserved

// Supporting and networking element types.
const MSG_STRING             = 0xA0;  // a node's label, keyed by node ID
const MSG_LAYOUT             = 0xA1;  // a node's position, keyed by node ID
const MSG_WIRE               = 0xA2;  // the shape of an output's wire, keyed by output ID
const MSG_DEVICE             = 0xB0;
const MSG_DEVICE_FIELD       = 0xB1;
const MSG_NETWORK            = 0xB2;
const MSG_NETWORK_FIELD      = 0xB3;

// Field IDs: the first payload byte of a 0xB0-0xB3 message.
const FIELD_DEVICE_DID       = 0x00;
const FIELD_DEVICE_NAME      = 0x01;
const FIELD_DEVICE_NETWORK   = 0x02;
const FIELD_NETWORK_OPTIONS  = 0x00;
const FIELD_NETWORK_NAME     = 0x01;
const FIELD_NETWORK_SSID     = 0x02;
const FIELD_NETWORK_PASSWORD = 0x03;  // secrets store only

// Command values: one byte, a fraction of full scale, 0x00 = 0 to 0xFF = 1.
const VALUE_FULL             = 0xFF;  // 1: a pressed button, a slider at its end
const VALUE_HALF             = 0x80;  // on/off outputs (LED, GPO) are on from here up

// Slider (MSG_UI_SLIDER) payload byte 0, bit 0: 0 = horizontal, 1 = vertical.
const SLIDER_VERTICAL        = 0x01;

// GPIO pins a GPO or PWM may use, bit n = GPIO n: GPIO 0-22 and 26-28, the ones on
// the Pico 2 W's header. GPIO 23, 24, 25 and 29 drive the WiFi chip.
const PIN_USABLE_MASK        = 0x1C7FFFFF;
const pinUsable = (pin) => Number.isInteger(pin) && pin >= 0 && pin < 32 && (PIN_USABLE_MASK >> pin & 1) === 1;
const USABLE_PINS = Array.from({ length: 32 }, (_, pin) => pin).filter(pinUsable);

// PWM output (MSG_HW_PWM) payload limits and options.
const PWM_FREQ_MIN           = 10;    // Hz; the slowest the PWM counter can run at 150 MHz is ~9 Hz
const PWM_DUTY_MAX           = 100;   // duty cycle in percent
const PWM_DUTY_INPUT         = 0x01;  // options bit 0: 0 = constant duty, 1 = duty from its input, min to max
const PWM_FREQ_INPUT         = 0x02;  // options bit 1: 0 = constant frequency, 1 = frequency from its input, min to max
const PWM_FREQ_LOG           = 0x04;  // options bit 2: a frequency from its input goes from min to max 0 = linearly, 1 = logarithmically

// Network options (FIELD_NETWORK_OPTIONS) bit 0: 0 = start an access point, 1 = join a network.
const NET_OPT_JOIN           = 0x01;
