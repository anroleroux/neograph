#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "tusb.h"
#include "device/usbd_pvt.h"    // usbd_defer_func

#include "keyboard.h"
#include "protocol.h"

#define KEYBOARD_NODES_MAX  16
#define KEYBOARD_QUEUE      8   // keystrokes waiting to be typed; divides 256

typedef struct {
    uint8_t mode;               // KEYBOARD_COMBO or KEYBOARD_TEXT
    uint8_t len;                // the bytes in keys: 2 for a combo, the characters for text
    uint8_t keys[KEYBOARD_TEXT_MAX];    // a combo's modifiers and key, or the text
} keystroke;

typedef struct {
    uint16_t id;
    keystroke stroke;
} keyboard_node;

static keyboard_node nodes[KEYBOARD_NODES_MAX];
static uint8_t node_count = 0;

// ── Typing ──
// The keystrokes waiting, oldest at tail. The main loop adds them (it alone moves
// head), and they are typed from TinyUSB's task (which alone moves tail).
//
// Every TinyUSB call is made from its task: core 0's background interrupt or a
// printf on either core runs that task, under pico_stdio_usb's lock, so calls
// from the main loop would race it. The main loop hands typing to the task with
// usbd_defer_func, and stdio_flush runs the task at once.
static keystroke queue[KEYBOARD_QUEUE];
static volatile uint8_t head = 0, tail = 0;

// The task's place in queue[tail]: the next report. Each key is two reports,
// down then up, so a key typed twice in a row reads as two presses.
static uint16_t report = 0;

static const uint8_t ascii_keys[128][2] = { HID_ASCII_TO_KEYCODE };

// Send the next report, if the PC is ready for it. Runs in TinyUSB's task:
// deferred from the main loop, or when the PC has taken the last report.
static void type_next(void *param) {
    (void)param;

    while (tail != head) {
        if (!tud_hid_ready()) return;    // busy, or the PC isn't listening

        const keystroke *k = &queue[tail % KEYBOARD_QUEUE];
        uint16_t keys = k->mode == KEYBOARD_COMBO ? 1 : k->len;
        if (report < 2 * keys) {
            uint8_t modifiers = 0;
            uint8_t codes[6] = {0};
            if (report % 2 == 0) {      // down; an up report is all zeros
                if (k->mode == KEYBOARD_COMBO) {
                    modifiers = k->keys[0];
                    codes[0] = k->keys[1];
                } else {
                    uint8_t c = k->keys[report / 2] & 0x7F;
                    modifiers = ascii_keys[c][0] ? KEYBOARD_MODIFIER_LEFTSHIFT : 0;
                    codes[0] = ascii_keys[c][1];
                }
            }
            if (tud_hid_keyboard_report(0, modifiers, codes)) report++;
            return;
        }

        report = 0;
        tail++;
    }
}

void tud_hid_report_complete_cb(uint8_t instance, const uint8_t *data, uint16_t len) {
    (void)instance; (void)data; (void)len;
    type_next(NULL);
}

// Unplugged: what is waiting would be typed, out of context, when it is
// plugged in again.
void tud_umount_cb(void) {
    report = 0;
    tail = head;
}

// The PC asks for a report or sets the keyboard's LEDs (Caps Lock and the like):
// neither is used.
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           const uint8_t *buffer, uint16_t bufsize) {
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;
}

// ── Main loop ──

void keyboard_clear(void) {
    node_count = 0;
}

bool keyboard_add(uint16_t id, const uint8_t *stroke, uint8_t len) {
    if (node_count == KEYBOARD_NODES_MAX || len < 2 || len - 1 > KEYBOARD_TEXT_MAX) return false;
    keyboard_node *n = &nodes[node_count++];
    n->id = id;
    n->stroke.mode = stroke[0];
    n->stroke.len = len - 1;
    memcpy(n->stroke.keys, stroke + 1, len - 1);
    return true;
}

void keyboard_press(uint16_t id) {
    const keyboard_node *n = NULL;
    for (uint8_t i = 0; i < node_count; i++) {
        if (nodes[i].id == id) n = &nodes[i];
    }
    if (!n) {
        printf("keyboard %d not found\n", id);
        return;
    }
    if (!tud_ready()) {
        printf("keyboard %d: no PC listening on USB, keystroke dropped\n", id);
        return;
    }
    if ((uint8_t)(head - tail) == KEYBOARD_QUEUE) {
        printf("keyboard %d: %d keystrokes waiting, keystroke dropped\n", id, KEYBOARD_QUEUE);
        return;
    }
    queue[head % KEYBOARD_QUEUE] = n->stroke;
    __dmb();
    head++;
    printf("keyboard %d: typing\n", id);
}

// Once each pass. No report is sent while the last is with the PC, so a spare
// call does nothing, and one that is lost is made again next pass.
bool keyboard_task(void) {
    if (tail == head) return false;
    usbd_defer_func(type_next, NULL, false);
    stdio_flush();
    return true;
}
