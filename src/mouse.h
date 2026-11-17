#ifndef MOUSE_H_INCLUDED
#define MOUSE_H_INCLUDED

#include <stdbool.h>

/*
 * USB mouse nodes (MSG_HW_MOUSE), on core 0. Core 1 runs nothing for them: each
 * pass of core 0's main loop reads their signals in the program core 1 runs (X, Y,
 * and the left, right and middle buttons) and moves the PC's pointer over USB.
 * PROTOCOL.md, "USB mice", has more.
 */

// Before the first mouse_task.
void mouse_init(void);

// Move the pointer and press the buttons as the mouse nodes' signals say; call it
// from core 0's main loop. True while the pointer moves or a report is waiting.
bool mouse_task(void);

// TinyUSB's HID callbacks for the mouse's interface (src/keyboard.c has them):
// the PC has taken a report, and the PC has gone.
void mouse_report_complete(void);
void mouse_unplugged(void);

#endif
