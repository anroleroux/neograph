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

// ── Messages (the body of a packet)
const MSG_HEADER_LEN         = 5;     // type (1), reserved (1), element ID (2, BE), payload length (1)

// Control element types (0x00-0x3F): bit 5 set = HW, bit 4 set = input.
const MSG_HW_BIT             = 0x20;
const MSG_INPUT_BIT          = 0x10;

const MSG_UI_GAUGE           = 0x00;  // reserved
const MSG_UI_INDICATOR       = 0x01;  // reserved
const MSG_UI_GRAPH           = 0x02;  // reserved
const MSG_UI_BUTTON          = 0x10;
const MSG_UI_SWITCH          = 0x11;  // reserved
const MSG_UI_SLIDER          = 0x12;  // reserved
const MSG_UI_SLIDER_2D       = 0x13;  // reserved
const MSG_UI_MULTI_SLIDER    = 0x14;  // reserved
const MSG_UI_DIAL            = 0x15;  // reserved
const MSG_UI_TIMER           = 0x16;  // reserved
const MSG_HW_LED             = 0x20;
const MSG_HW_GPO             = 0x21;
const MSG_HW_PWM             = 0x22;  // reserved
const MSG_HW_CORE            = 0x30;  // reserved
const MSG_HW_GPI             = 0x31;  // reserved
const MSG_HW_ADC             = 0x32;  // reserved

// Supporting and networking element types.
const MSG_STRING             = 0xA0;  // reserved
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

// Network options (FIELD_NETWORK_OPTIONS) bit 0: 0 = start an access point, 1 = join a network.
const NET_OPT_JOIN           = 0x01;
