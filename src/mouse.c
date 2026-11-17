#include <math.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "pico/sync.h"
#include "tusb.h"
#include "device/usbd_pvt.h"    // usbd_defer_func

#include "hwman.h"
#include "mouse.h"
#include "program.h"
#include "protocol.h"

#define MOUSE_DEAD_ZONE     0.02f   // of X's or Y's tilt, still: a 2D slider's home, VALUE_HALF, is 0.502
#define MOUSE_STEP_MAX      127     // counts one report moves, as a HID mouse's 8-bit X and Y allow
#define MOUSE_KEPT_MAX      (4 * MOUSE_STEP_MAX)    // counts waiting for the PC: a few reports
#define MOUSE_PASS_MAX_US   50000   // the longest time one pass moves for, so a pass after a
                                    // flash write doesn't jump

// ── Reports ──
// What the PC has still to be sent: counts to move, and the buttons. The main loop
// adds to it; reports are sent from TinyUSB's task, which takes from it. As for the
// keyboard (src/keyboard.c), every TinyUSB call is made from its task, which may run
// on either core (a printf runs it), so the two meet under a lock.
static critical_section_t lock;
static int32_t waiting[2];          // X, then Y, as HID counts them: right and down
static uint8_t buttons;             // MOUSE_BUTTON_ bits now
static uint8_t sent_buttons;        // and as the PC last had them

static int8_t step(int32_t counts) {
    return (int8_t)(counts > MOUSE_STEP_MAX ? MOUSE_STEP_MAX : counts < -MOUSE_STEP_MAX ? -MOUSE_STEP_MAX : counts);
}

// Send the next report, if there is one and the PC is ready for it. Runs in TinyUSB's
// task: deferred from the main loop, or when the PC has taken the last report.
static void send_next(void *param) {
    (void)param;
    if (!tud_hid_n_ready(HID_INSTANCE_MOUSE)) return;      // busy, or the PC isn't listening

    critical_section_enter_blocking(&lock);
    int8_t dx = step(waiting[0]), dy = step(waiting[1]);
    uint8_t b = buttons;
    bool due = dx || dy || b != sent_buttons;
    critical_section_exit(&lock);
    if (!due || !tud_hid_n_mouse_report(HID_INSTANCE_MOUSE, 0, b, dx, dy, 0, 0)) return;

    critical_section_enter_blocking(&lock);
    waiting[0] -= dx;
    waiting[1] -= dy;
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
    waiting[0] = waiting[1] = 0;
    sent_buttons = 0;
    critical_section_exit(&lock);
}

// ── Main loop ──

static float part[2];               // a count not yet whole, X then Y
static uint64_t last_us;

void mouse_init(void) {
    critical_section_init(&lock);
    last_us = time_us_64();
}

// How far X or Y is tilted from still, -1 to 1: its value, from 0 to 1 with
// MOUSE_CENTRED, or -1 to 1; beyond those, as at the end. Within MOUSE_DEAD_ZONE of
// still it is still, and the tilt grows from there.
static float tilt(float v, bool centred) {
    if (centred) v = 2.0f * v - 1.0f;
    if (!isfinite(v)) return 0;
    float a = fminf(fabsf(v), 1.0f);
    if (a <= MOUSE_DEAD_ZONE) return 0;
    return copysignf((a - MOUSE_DEAD_ZONE) / (1.0f - MOUSE_DEAD_ZONE), v);
}

bool mouse_task(void) {
    uint64_t now = time_us_64();
    float dt = (float)(now - last_us < MOUSE_PASS_MAX_US ? now - last_us : MOUSE_PASS_MAX_US) / 1e6f;
    last_us = now;

    // Every mouse node moves the one pointer: their speeds add, and a button is
    // pressed while any presses it. An input wired to nothing is still, or not pressed.
    const hw_program *p = program_running(NULL);
    float speed[2] = { 0, 0 };      // counts a second
    uint8_t b = 0;
    for (uint8_t i = 0; i < p->node_count; i++) {
        const prog_node *n = &p->nodes[i];
        if (n->type != MSG_HW_MOUSE) continue;
        for (int a = 0; a < 2; a++) {
            if (n->in[a] != PROG_NO_SIGNAL)
                speed[a] += tilt(p->signals[n->in[a]].value, n->options & MOUSE_CENTRED) * n->speed;
        }
        static const uint8_t bits[3] = { MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE };
        for (int k = 0; k < 3; k++) {
            if (n->in[2 + k] != PROG_NO_SIGNAL && p->signals[n->in[2 + k]].value >= 0.5f) b |= bits[k];
        }
    }
    speed[1] = -speed[1];           // Y up moves the pointer up; HID counts down

    if (!tud_ready()) {             // no PC: nothing builds up for later
        part[0] = part[1] = 0;
        mouse_unplugged();
        return false;
    }

    int32_t move[2];
    for (int a = 0; a < 2; a++) {
        part[a] += speed[a] * dt;
        move[a] = (int32_t)part[a];     // whole counts, toward 0
        part[a] -= (float)move[a];
    }

    critical_section_enter_blocking(&lock);
    for (int a = 0; a < 2; a++) {
        int32_t w = waiting[a] + move[a];
        waiting[a] = w > MOUSE_KEPT_MAX ? MOUSE_KEPT_MAX : w < -MOUSE_KEPT_MAX ? -MOUSE_KEPT_MAX : w;
    }
    buttons = b;
    bool due = waiting[0] || waiting[1] || buttons != sent_buttons;
    critical_section_exit(&lock);

    if (due) {
        usbd_defer_func(send_next, NULL, false);
        stdio_flush();              // runs TinyUSB's task now
    }
    return due || speed[0] != 0 || speed[1] != 0;
}
