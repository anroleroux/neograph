#ifndef MOUSE_H_INCLUDED
#define MOUSE_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

/*
 * The USB mouse (MSG_HW_MOUSE), on core 0. Its pad in the Command view sends
 * commands (PKT_COMMAND, MOUSE_REPORT_LEN bytes): the buttons and keyboard modifiers
 * held, and how far to move the pointer and turn the wheels. Core 0 sends them to the
 * PC over USB; core 1 and the graph take no part. PROTOCOL.md, "USB mice", has more.
 */

// Before the first mouse_task.
void mouse_init(void);

// A command from the mouse's pad: a payload mouse_report_valid accepts.
void mouse_command(const uint8_t *pay);

// Keep the PC up to date, and let go of everything held once commands stop for
// MOUSE_HOLD_MS; call it from core 0's main loop. True while a report is waiting.
bool mouse_task(void);

// TinyUSB's HID callbacks for the mouse's interface (src/keyboard.c has them):
// the PC has taken a report, and the PC has gone.
void mouse_report_complete(void);
void mouse_unplugged(void);

#endif
