#ifndef TUSB_CONFIG_H_INCLUDED
#define TUSB_CONFIG_H_INCLUDED

/*
 * TinyUSB configuration. The device is a composite of the serial console (CDC),
 * which pico_stdio_usb drives, and a keyboard (HID), which src/keyboard.c drives.
 * src/usb_descriptors.c describes the two to the PC.
 */

#define CFG_TUSB_RHPORT0_MODE       OPT_MODE_DEVICE

#define CFG_TUD_ENDPOINT0_SIZE      64

#define CFG_TUD_CDC                 1
#define CFG_TUD_HID                 1
#define CFG_TUD_MSC                 0
#define CFG_TUD_MIDI                0
#define CFG_TUD_VENDOR              0

#define CFG_TUD_CDC_RX_BUFSIZE      64
#define CFG_TUD_CDC_TX_BUFSIZE      64
#define CFG_TUD_CDC_EP_BUFSIZE      64

#define CFG_TUD_HID_EP_BUFSIZE      8   /* a boot keyboard report */

#endif
