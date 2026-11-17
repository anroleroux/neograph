#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/sync.h"
#include "tusb.h"
#include "device/usbd_pvt.h"    // usbd_defer_func

#include "keyboard.h"
#include "mouse.h"
#include "protocol.h"

#define MOUSE_STEP_MAX      127     // counts or clicks one report moves, as a HID mouse's 8-bit fields allow
#define MOUSE_KEPT_MAX      4000    // counts waiting for the PC: a fast flick, sent over a few reports

// ── Reports ──
// What the PC has still to be sent: counts to move, wheel clicks, and the buttons.
// Commands add to it from the main loop; reports are sent from TinyUSB's task, which
// takes from it. As for the keyboard (src/keyboard.c), every TinyUSB call is made
// from its task, which may run on either core (a printf runs it), so the two meet
// under a lock.
enum { AXIS_X, AXIS_Y, AXIS_WHEEL, AXIS_PAN, AXES };
static critical_section_t lock;
static int32_t waiting[AXES];       // X right, Y down, the wheel up, the horizontal wheel right
static uint8_t buttons;             // MOUSE_BUTTONS held now
static uint8_t sent_buttons;        // and as the PC last had them

static int8_t step(int32_t n) {
    return (int8_t)(n > MOUSE_STEP_MAX ? MOUSE_STEP_MAX : n < -MOUSE_STEP_MAX ? -MOUSE_STEP_MAX : n);
}

static bool due(void) {
    return waiting[AXIS_X] || waiting[AXIS_Y] || waiting[AXIS_WHEEL] || waiting[AXIS_PAN] || buttons != sent_buttons;
}

// Send the next report, if there is one and the PC is ready for it. Runs in TinyUSB's
// task: deferred from the main loop, or when the PC has taken the last report.
static void send_next(void *param) {
    (void)param;
    if (!tud_hid_n_ready(HID_INSTANCE_MOUSE)) return;      // busy, or the PC isn't listening

    critical_section_enter_blocking(&lock);
    int8_t d[AXES];
    for (int a = 0; a < AXES; a++) d[a] = step(waiting[a]);
    uint8_t b = buttons;
    bool any = due();
    critical_section_exit(&lock);
    if (!any || !tud_hid_n_mouse_report(HID_INSTANCE_MOUSE, 0, b, d[AXIS_X], d[AXIS_Y], d[AXIS_WHEEL], d[AXIS_PAN])) return;

    critical_section_enter_blocking(&lock);
    for (int a = 0; a < AXES; a++) waiting[a] -= d[a];
    sent_buttons = b;
    critical_section_exit(&lock);
}

void mouse_report_complete(void) {
    send_next(NULL);
}

// Unplugged: a move waiting would jump the pointer when it is plugged in again, and
// the PC has let go of the buttons.
void mouse_unplugged(void) {
    critical_section_enter_blocking(&lock);
    for (int a = 0; a < AXES; a++) waiting[a] = 0;
    sent_buttons = 0;
    critical_section_exit(&lock);
}

// ── Main loop ──

static absolute_time_t hold_until;  // let go of what is held then, unless a command comes
static bool holding;                // buttons or modifiers are held

void mouse_init(void) {
    critical_section_init(&lock);
}

void mouse_command(const uint8_t *pay) {
    int32_t add[AXES] = {
        (int16_t)(pay[2] << 8 | pay[3]),
        (int16_t)(pay[4] << 8 | pay[5]),
        (int8_t)pay[6],
        (int8_t)pay[7],
    };
    if (!tud_ready()) return;       // no PC: nothing builds up for later

    critical_section_enter_blocking(&lock);
    for (int a = 0; a < AXES; a++) {
        int32_t w = waiting[a] + add[a];
        waiting[a] = w > MOUSE_KEPT_MAX ? MOUSE_KEPT_MAX : w < -MOUSE_KEPT_MAX ? -MOUSE_KEPT_MAX : w;
    }
    buttons = pay[0];
    critical_section_exit(&lock);
    keyboard_hold(pay[1]);

    holding = pay[0] || pay[1];
    hold_until = make_timeout_time_ms(MOUSE_HOLD_MS);
}

bool mouse_task(void) {
    // The pad went quiet with something held (the browser closed, the WiFi dropped):
    // let go, so no button or Shift sticks on the PC.
    if (holding && time_reached(hold_until)) {
        printf("mouse: no command for %u ms, letting go\n", MOUSE_HOLD_MS);
        critical_section_enter_blocking(&lock);
        buttons = 0;
        critical_section_exit(&lock);
        keyboard_hold(0);
        holding = false;
    }

    if (!tud_ready()) {
        mouse_unplugged();
        return false;
    }

    critical_section_enter_blocking(&lock);
    bool any = due();
    critical_section_exit(&lock);
    if (any) {
        usbd_defer_func(send_next, NULL, false);
        stdio_flush();              // runs TinyUSB's task now
    }
    return any;
}
