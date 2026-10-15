#include "pico/unique_id.h"
#include "tusb.h"

/*
 * The USB descriptors: one device with two functions, the serial console (CDC,
 * two interfaces) and a boot keyboard (HID, one interface). pico_stdio_usb's own
 * descriptors describe the console alone, so this file replaces them
 * (PICO_STDIO_USB_USE_DEFAULT_DESCRIPTORS is 0 when the app links TinyUSB).
 */

/* TinyUSB's test vendor ID, with a product ID for a CDC + HID device. A board sold
   to others needs a vendor and product ID of its own. */
#define USBD_VID            0xCAFE
#define USBD_PID            0x4005

#define ITF_CDC             0       /* and 1 */
#define ITF_HID             2
#define ITF_COUNT           3

#define EP_CDC_NOTIF        0x81
#define EP_CDC_OUT          0x02
#define EP_CDC_IN           0x82
#define EP_HID_IN           0x83

#define HID_POLL_MS         1       /* how often the PC asks for a report: a key down or up each time */

enum {
    STR_LANGUAGE,
    STR_MANUFACTURER,
    STR_PRODUCT,
    STR_SERIAL,
    STR_CDC,
    STR_HID,
    STR_COUNT
};

static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    /* Interface association descriptors group the CDC's two interfaces. */
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USBD_VID,
    .idProduct          = USBD_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = STR_MANUFACTURER,
    .iProduct           = STR_PRODUCT,
    .iSerialNumber      = STR_SERIAL,
    .bNumConfigurations = 1,
};

static const uint8_t desc_hid_report[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

#define CONFIG_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_DESC_LEN)

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, CONFIG_LEN, 0, 250),
    TUD_CDC_DESCRIPTOR(ITF_CDC, STR_CDC, EP_CDC_NOTIF, 8, EP_CDC_OUT, EP_CDC_IN, 64),
    TUD_HID_DESCRIPTOR(ITF_HID, STR_HID, HID_ITF_PROTOCOL_KEYBOARD, sizeof desc_hid_report,
                       EP_HID_IN, CFG_TUD_HID_EP_BUFSIZE, HID_POLL_MS),
};

_Static_assert(sizeof desc_configuration == CONFIG_LEN, "configuration descriptor length");

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&desc_device;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return desc_hid_report;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    static uint16_t desc[1 + 32];

    static const char *const strings[STR_COUNT] = {
        [STR_MANUFACTURER] = "neoGraph",
        [STR_PRODUCT]      = "neoGraph",
        [STR_SERIAL]       = serial,
        [STR_CDC]          = "neoGraph console",
        [STR_HID]          = "neoGraph keyboard",
    };

    uint8_t len;
    if (index == STR_LANGUAGE) {
        desc[1] = 0x0409;   /* English */
        len = 1;
    } else {
        if (index >= STR_COUNT) return NULL;
        if (!serial[0]) pico_get_unique_board_id_string(serial, sizeof serial);
        const char *s = strings[index];
        for (len = 0; len < 32 && s[len]; len++) desc[1 + len] = s[len];
    }
    desc[0] = (uint16_t)(TUSB_DESC_STRING << 8 | (2 * len + 2));
    return desc;
}
