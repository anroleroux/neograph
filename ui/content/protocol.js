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

// Message header byte 1, bit 1: a hardware input (core temperature, ADC, GPIO input, DS18B20,
// rotary encoder, clock) or
// a logic element shows a tile in the Command view (MSG_FLAG_SHOWN). The device ignores it.
const MSG_FLAG_SHOWN         = 0x02;

// Message header byte 1, bit 2: a part of a block (MSG_FLAG_PART): logic the UI wrote
// for one copy of a block (MSG_SUB). The device runs it as any logic; the UI skips it
// when it restores the graph, and builds the block's parts again from its definition.
const MSG_FLAG_PART          = 0x04;

// A 3-byte source reference's output byte, bit 7: the wire is a feedback wire, which
// closes a loop. Logic reads it as the last scan left it. Bits 0-6 are the output.
const MSG_REF_FEEDBACK       = 0x80;

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
const MSG_HW_STEPPER         = 0x24;  // a stepper motor on four pins, through a ULN2003
const MSG_HW_SERVO           = 0x25;  // a hobby servo, such as an SG90, on a PWM pin
const MSG_HW_NEOPIXEL        = 0x26;  // NeoPixels (WS2812): an array, circle or doughnut, showing a graphic
const MSG_HW_CORE            = 0x30;  // the chip's temperature
const MSG_HW_GPI             = 0x31;  // a GPIO input pin
const MSG_HW_ADC             = 0x32;  // an analog input pin
const MSG_HW_DS18B20         = 0x33;  // a DS18B20 temperature sensor on a 1-Wire pin; two outputs
const MSG_HW_ENCODER         = 0x34;  // a rotary encoder and its push switch; two outputs
const MSG_HW_CLOCK           = 0x35;  // the device's clock, a time of day: hour, minute, second

// Logic element types (0x40-0x7F): run on the device. Each reads sources and is a
// source itself, with one output, 0: boolean, or floating for a scale, min or max.
const MSG_LOGIC_BIT          = 0x40;
const MSG_LOGIC_COMPARE      = 0x40;  // a floating input above or below a threshold
const MSG_LOGIC_AND          = 0x41;  // on while every input is on
const MSG_LOGIC_OR           = 0x42;  // on while any input is on
const MSG_LOGIC_NOT          = 0x43;  // on while its input is off
const MSG_LOGIC_DELAY        = 0x44;  // its input, after an on delay and an off delay
const MSG_LOGIC_SCALE        = 0x45;  // floating: its input mapped from one range onto another
const MSG_LOGIC_MIN          = 0x46;  // floating: the least of its inputs
const MSG_LOGIC_MAX          = 0x47;  // floating: the greatest of its inputs
const MSG_LOGIC_PULSE        = 0x48;  // on for a time after its input goes on
const MSG_LOGIC_BLINK        = 0x49;  // on and off in turn while its input is on
const MSG_LOGIC_TOGGLE       = 0x4A;  // turns over each time its input goes on
const MSG_LOGIC_LATCH        = 0x4B;  // on once set, off once reset; reset wins
const MSG_LOGIC_COUNTER      = 0x4C;  // counts its input going on, up to a preset; two outputs
const MSG_LOGIC_SINE         = 0x4D;  // floating: a sine wave, while its input is on
const MSG_LOGIC_LAST         = 0x4D;  // the last logic type: MSG_LOGIC_SINE

// Blocks (subroutines): see src/protocol.h.
const MSG_SUB                = 0x60;  // a copy of a block
const MSG_SUB_INPUT          = 0x61;  // in a definition: an input, its type then its index
const MSG_SUB_OUTPUT         = 0x62;  // in a definition: an output, its source, type and index
const SUB_PORTS_MAX          = 8;     // inputs, and outputs, of a block
const SUB_TYPE_BOOL          = 0x00;  // a port's type: on/off
const SUB_TYPE_FLOAT         = 0x01;  // a port's type: a number

// Supporting and networking element types.
const MSG_STRING             = 0xA0;  // a node's label, keyed by node ID
const MSG_LAYOUT             = 0xA1;  // a node's position, keyed by node ID
const MSG_WIRE               = 0xA2;  // the shape of an output's wire, keyed by output ID
const MSG_SUB_DEF            = 0xA3;  // a block's definition: its name, keyed by definition ID
const MSG_SUB_PART           = 0xA4;  // one message of a block's definition, whole, keyed by definition ID
const MSG_DEVICE             = 0xB0;
const MSG_DEVICE_FIELD       = 0xB1;
const MSG_NETWORK            = 0xB2;
const MSG_NETWORK_FIELD      = 0xB3;

// Field IDs: the first payload byte of a 0xB0-0xB3 message.
const FIELD_DEVICE_DID       = 0x00;
const FIELD_DEVICE_NAME      = 0x01;
const FIELD_DEVICE_NETWORK   = 0x02;
const FIELD_DEVICE_OPTIONS   = 0x03;  // 1 byte, DEV_OPT_ bits; without it, 0
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

// DS18B20 temperature sensor (MSG_HW_DS18B20) payload byte 0: its GPIO pin, the 1-Wire
// data line. Byte 1: options: bits 0-1 its resolution (DS18B20_RES_), bit 2 the pin's
// internal pull-up. Outputs: its temperature in °C (0), whether the sensor answers (1).
const DS18B20_RES_MASK       = 0x03;
const DS18B20_RES_9          = 0x00;  // 0.5 °C steps, a reading every 94 ms
const DS18B20_RES_10         = 0x01;  // 0.25 °C, 188 ms
const DS18B20_RES_11         = 0x02;  // 0.125 °C, 375 ms
const DS18B20_RES_12         = 0x03;  // 0.0625 °C, 750 ms
const DS18B20_PULLUP         = 0x04;
const DS18B20_FULL           = 100;   // °C: its full scale, for an output that takes a fraction

// Rotary encoder (MSG_HW_ENCODER) payload: the GPIO of its A (CLK), B (DT) and push
// switch (SW, or ENCODER_NO_BUTTON), its options, then its steps (2 bytes, BE). Options:
// bits 0-1 its transitions a click (ENCODER_DETENT_), bit 2 it counts the other way,
// bit 3 it wraps past its ends. Outputs: its position, 0 to its steps (0), pressed (1).
const ENCODER_PAYLOAD_LEN    = 6;
const ENCODER_NO_BUTTON      = 0xFF;
const ENCODER_DETENT_MASK    = 0x03;
const ENCODER_DETENT_4       = 0x00;  // a whole cycle of its pins a click: most encoders, a KY-040
const ENCODER_DETENT_2       = 0x01;  // half a cycle a click
const ENCODER_DETENT_1       = 0x02;  // each change of a pin; or an encoder without clicks
const ENCODER_REVERSE        = 0x04;
const ENCODER_WRAP           = 0x08;
const ENCODER_STEPS_MAX      = 1000;

// Clock (MSG_HW_CLOCK): the device has one clock, which every clock node reads. Outputs:
// its hour (0), minute (1) and second (2), whole numbers. Its payload: options
// (CLOCK_SYNC: internet time), then its time zone, minutes ahead of UTC, int16 BE. A
// command from a clock sets it: CLOCK_SET_LEN bytes, the hour, minute and second.
const CLOCK_HOURS            = 24;
const CLOCK_MINUTES          = 60;
const CLOCK_SECONDS          = 60;
const CLOCK_SET_LEN          = 3;
const CLOCK_PAYLOAD_LEN      = 3;
const CLOCK_SYNC             = 0x01;  // options bit 0: internet time
const CLOCK_ZONE_MIN         = -720;  // UTC-12
const CLOCK_ZONE_MAX         = 840;   // UTC+14
const CLOCK_ZONE_STEP        = 15;    // minutes: zones such as UTC+5:45

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
const SCALE_CLAMP            = 0x01;  // options bit 0: keep the output within its range
const COUNTER_MAX            = 1000000;  // the highest preset
const SINE_PERIOD_MIN_MS     = 20;    // a sine's shortest period: two scans

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

// Stepper (MSG_HW_STEPPER) payload options and limits. Bits 0-1 of the options: how
// its coils are driven (wave, full or half steps).
const STEPPER_DRIVE_MASK     = 0x03;
const STEPPER_WAVE           = 0x00;  // one coil at a time: 4 steps a cycle, the least current
const STEPPER_FULL           = 0x01;  // two at a time: 4 steps a cycle, the most torque
const STEPPER_HALF           = 0x02;  // one then two in turn: 8 steps a cycle, twice the steps, the smoothest
const STEPPER_SPEED          = 0x04;  // options bit 2: 0 = its input sets where it is, in revolutions; 1 = how fast, in RPM
const STEPPER_REVERSE        = 0x08;  // options bit 3: turn the other way
const STEPPER_RELEASE        = 0x10;  // options bit 4: 0 = hold its coils on when it stops; 1 = turn them off
const STEPPER_PINS           = 4;     // IN1 to IN4 of the ULN2003
const STEPPER_STEPS_MIN      = 4;     // full steps per revolution, at least
const STEPPER_RPM_MAX        = 300;   // the highest top speed, revolutions per minute
const STEPPER_RATE_MAX       = 2000;  // steps per second the device steps at most

// Servo (MSG_HW_SERVO) payload limits. It runs its pin's PWM slice at SERVO_FREQ, so it
// takes the slice as a PWM does.
const SERVO_FREQ             = 50;    // Hz: a 20 ms frame
const SERVO_ANGLE_MAX        = 180;   // degrees
const SERVO_PULSE_MIN        = 100;   // µs, the shortest pulse it may be set to
const SERVO_PULSE_MAX        = 3000;  // µs, the longest
const SERVO_SPEED_MAX        = 3600;  // °/s, the highest speed limit; 0 = none

// NeoPixel (MSG_HW_NEOPIXEL) payload: a chain of WS2812 LEDs on one pin, laid out as an
// array, a circle or a doughnut, showing a graphic of its input while its on/off input
// is on (or not wired).
const NEOPIXEL_ARRAY         = 0x00;  // layout: rows × columns; LED 0 at the top left, along the top row
const NEOPIXEL_CIRCLE        = 0x01;  // layout: 1 to NEOPIXEL_RINGS_MAX rings, in the order they are wired
const NEOPIXEL_DOUGHNUT      = 0x02;  // layout: one ring of LEDs
const NEOPIXEL_ZIGZAG        = 0x01;  // options bit 0, an array: every other row runs back, right to left
const NEOPIXEL_RINGS_MAX     = 8;     // a circle's rings
const NEOPIXEL_LEDS_MAX      = 256;   // LEDs on one node
const NEOPIXEL_GFX_BAR       = 0x00;  // graphic: a bar level indicator
const NEOPIXEL_GFX_POINT     = 0x01;  // graphic: one LED, moving from a start to an end as its input goes 0 to 1
const NEOPIXEL_GFX_PATCH     = 0x02;  // graphic: every LED from a start to an end, lit; its input may set its colour
const NEOPIXEL_GFX_REVERSE   = 0x01;  // graphic options bit 0: it runs the other way
const NEOPIXEL_GFX_COLOUR_INPUT = 0x02;  // graphic options bit 1: its colour goes from a colour at 0 to one at 1
const NEOPIXEL_BAR_LEN       = 6;     // a bar's settings: along, position, options, then its colour
const NEOPIXEL_POINT_LEN     = 8;     // a point's settings: its start and end, options, then its colour
const NEOPIXEL_PATCH_LEN     = 8;     // a patch's settings: its start and end corners, options, then its colour
const NEOPIXEL_COLOUR_LEN    = 3;     // red, green, blue; a second colour with NEOPIXEL_GFX_COLOUR_INPUT
const NEOPIXEL_BAR_ROW       = 0x00;  // a bar along an array's row; around a circle's ring; around a doughnut
const NEOPIXEL_BAR_COLUMN    = 0x01;  // a bar up an array's column; across a circle's rings, from the centre out

// Device options (FIELD_DEVICE_OPTIONS) bit 0: 0 = TCP holds a small frame back while an
// earlier one is unacknowledged (Nagle's algorithm), 1 = each frame is sent at once.
const DEV_OPT_NODELAY        = 0x01;

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
