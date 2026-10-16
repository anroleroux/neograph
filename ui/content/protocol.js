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
const MSG_HEADER_LEN         = 5;     // type (1), flags (1), element ID (2, BE), payload length (1)

// Message header byte 1, bit 0: each source reference in the payload is 3 bytes, the
// element ID then the index of its output (MSG_FLAG_OUTPUT). Without it, a reference
// is the 2-byte element ID, and means the element's output 0: messages saved before
// sources had several outputs.
const MSG_FLAG_OUTPUT        = 0x01;

// Message header byte 1, bit 1: a hardware input (core temperature, ADC, GPIO input) or
// a logic element shows a tile in the Command view (MSG_FLAG_SHOWN). The device ignores it.
const MSG_FLAG_SHOWN         = 0x02;

// Control element types (0x00-0x7F): bit 6 set = logic (below); otherwise bit 5 set =
// HW, bit 4 set = input.
const MSG_HW_BIT             = 0x20;
const MSG_INPUT_BIT          = 0x10;

const MSG_UI_GAUGE           = 0x00;  // reserved
const MSG_UI_INDICATOR       = 0x01;  // a light, drawn in the browser
const MSG_UI_GRAPH           = 0x02;  // a plot, drawn in the browser
const MSG_UI_BUTTON          = 0x10;
const MSG_UI_SWITCH          = 0x11;  // reserved
const MSG_UI_SLIDER          = 0x12;
const MSG_UI_SLIDER_2D       = 0x13;  // two outputs: X (0) and Y (1)
const MSG_UI_MULTI_SLIDER    = 0x14;  // reserved
const MSG_UI_DIAL            = 0x15;  // reserved
const MSG_UI_TIMER           = 0x16;  // reserved
const MSG_HW_LED             = 0x20;
const MSG_HW_GPO             = 0x21;
const MSG_HW_PWM             = 0x22;
const MSG_HW_KEYBOARD        = 0x23;  // a keystroke typed to the PC over USB
const MSG_HW_CORE            = 0x30;  // the chip's temperature
const MSG_HW_GPI             = 0x31;  // a GPIO input pin
const MSG_HW_ADC             = 0x32;  // an analog input pin

// Logic element types (0x40-0x7F): run on the device. Each reads sources and is a
// boolean source itself, with one output, 0.
const MSG_LOGIC_BIT          = 0x40;
const MSG_LOGIC_COMPARE      = 0x40;  // a floating input above or below a threshold
const MSG_LOGIC_AND          = 0x41;  // on while every input is on
const MSG_LOGIC_OR           = 0x42;  // on while any input is on
const MSG_LOGIC_NOT          = 0x43;  // on while its input is off
const MSG_LOGIC_DELAY        = 0x44;  // its input, after an on delay and an off delay

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
const FIELD_NETWORK_AP_ADDRESS     = 0x04;  // access point: its IPv4 address, 4 bytes
const FIELD_NETWORK_STATIC_ADDRESS = 0x05;  // joining: a fixed IPv4 address, 4 bytes
const FIELD_NETWORK_GATEWAY        = 0x06;  // joining with a fixed address: the gateway, 4 bytes

// Command values: one byte, a fraction of full scale, 0x00 = 0 to 0xFF = 1.
const VALUE_FULL             = 0xFF;  // 1: a pressed button, a slider at its end
const VALUE_HALF             = 0x80;  // on/off outputs (LED, GPO) are on from here up

// Slider (MSG_UI_SLIDER) payload byte 0, bit 0: 0 = horizontal, 1 = vertical.
const SLIDER_VERTICAL        = 0x01;

// 2D slider (MSG_UI_SLIDER_2D) payload byte 0, bit 0: 0 = it stays where it is let go,
// 1 = it returns to the centre (VALUE_HALF on both outputs). Bits 1-2: how it returns.
// Byte 1: its return speed, 1-255 % of its range per second. Bytes 2-3: its home, X then
// Y as values (0 to VALUE_FULL): where it starts, and where it returns to.
const SLIDER_RETURN          = 0x01;
const SLIDER_EASE_MASK       = 0x06;
const SLIDER_EASE_IMMEDIATE  = 0x00;  // at once
const SLIDER_EASE_LINEAR     = 0x02;  // at a steady speed
const SLIDER_EASE_SIGMOID    = 0x04;  // speeding up, then slowing down (smoothstep)

// Button (MSG_UI_BUTTON) payload byte 0, bit 0: 0 = each click toggles it, 1 = it is on
// while held. A button without the byte is a toggle.
const BUTTON_MOMENTARY       = 0x01;

// GPIO pins a GPO or PWM may use, bit n = GPIO n: GPIO 0-22 and 26-28, the ones on
// the Pico 2 W's header. GPIO 23, 24, 25 and 29 drive the WiFi chip.
const PIN_USABLE_MASK        = 0x1C7FFFFF;
const pinUsable = (pin) => Number.isInteger(pin) && pin >= 0 && pin < 32 && (PIN_USABLE_MASK >> pin & 1) === 1;
const USABLE_PINS = Array.from({ length: 32 }, (_, pin) => pin).filter(pinUsable);

// ADC input (MSG_HW_ADC) payload byte 0: the channel, 0 to ADC_CHANNELS - 1, on GPIO
// ADC_PIN_FIRST + channel. ADC3, on GPIO 29, is the WiFi chip's clock on the Pico 2 W.
const ADC_CHANNELS           = 3;
const ADC_PIN_FIRST          = 26;
const ADC_RAW_FULL           = 4095;  // a 12-bit reading at the reference voltage, 3.3 V

// Logic elements: a compare's condition; the inputs of an AND or OR; a delay's longest, in ms.
const COMPARE_ABOVE          = 0x00;  // on above the threshold; off below it less the hysteresis
const COMPARE_BELOW          = 0x01;  // on below the threshold; off above it plus the hysteresis
const LOGIC_INPUTS_MAX       = 4;
const DELAY_MAX_MS           = 86400000;  // a day

// Core temperature: on the device it drives outputs from 0 °C (0) to CORE_TEMP_FULL °C (1).
const CORE_TEMP_FULL         = 100;

// GPIO input (MSG_HW_GPI) payload byte 0: the GPIO pin, a usable one (pinUsable). Byte 1:
// options. Bits 0-1: the pin's pull (GPI_PULL_). Bit 2 (GPI_INVERT): 0 = on while the pin
// is high, 1 = on while it is low.
const GPI_PULL_MASK          = 0x03;
const GPI_PULL_NONE          = 0x00;
const GPI_PULL_UP            = 0x01;
const GPI_PULL_DOWN          = 0x02;
const GPI_INVERT             = 0x04;

// USB keyboard (MSG_HW_KEYBOARD) payload byte 2: what it types each time its input goes on.
const KEYBOARD_COMBO         = 0x00;  // one key with modifiers: byte 3 the HID modifier bits, byte 4 the HID key
const KEYBOARD_TEXT          = 0x01;  // text: bytes 3… printable ASCII, \t or \n, typed on a US layout
const KEYBOARD_TEXT_MAX      = 64;    // characters in a text keystroke

// PWM output (MSG_HW_PWM) payload limits and options.
const PWM_FREQ_MIN           = 10;    // Hz; the slowest the PWM counter can run at 150 MHz is ~9 Hz
const PWM_DUTY_MAX           = 100;   // duty cycle in percent
const PWM_DUTY_INPUT         = 0x01;  // options bit 0: 0 = constant duty, 1 = duty from its input, min to max
const PWM_FREQ_INPUT         = 0x02;  // options bit 1: 0 = constant frequency, 1 = frequency from its input, min to max
const PWM_FREQ_LOG           = 0x04;  // options bit 2: a frequency from its input goes from min to max 0 = linearly, 1 = logarithmically

// Network options (FIELD_NETWORK_OPTIONS) bit 0: 0 = start an access point, 1 = join a network.
const NET_OPT_JOIN           = 0x01;

// IPv4 address fields: 4 bytes, most significant first; 0.0.0.0 = not set. The access
// point's network is its address's /24, and its DHCP server leases the
// NET_LEASE_COUNT addresses from host NET_LEASE_FIRST, so the access point may not
// take one of those.
const NET_LEASE_FIRST        = 16;
const NET_LEASE_COUNT        = 8;
const NET_AP_ADDRESS_DEFAULT = '192.168.4.1';   // the access point's address when none is set

// An IPv4 address from its text, as 4 bytes; null if it isn't one.
function parseAddress(text) {
    const parts = String(text).trim().split('.');
    if (parts.length !== 4 || !parts.every(p => /^\d{1,3}$/.test(p) && Number(p) <= 255)) return null;
    return parts.map(Number);
}
// A unicast address that isn't loopback, the network (.0) or the broadcast (.255) of its /24.
const addressUsable = (a) => a[0] !== 0 && a[0] !== 127 && a[0] < 224 && a[3] !== 0 && a[3] !== 255;
// An address an access point may take: usable, and not one its DHCP server leases.
const apAddressUsable = (a) => addressUsable(a) && (a[3] < NET_LEASE_FIRST || a[3] >= NET_LEASE_FIRST + NET_LEASE_COUNT);
