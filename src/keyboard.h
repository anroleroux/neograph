#ifndef KEYBOARD_H_INCLUDED
#define KEYBOARD_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

/*
 * USB keyboard nodes (MSG_HW_KEYBOARD), on core 0. The config gives each node a
 * keystroke; core 1 says when a node's input goes on (FIFO_TAG_KEYBOARD), and its
 * keystroke is typed to the PC over USB. PROTOCOL.md, "USB keyboards", has more.
 */

// Forget every node's keystroke: a config replaces them.
void keyboard_clear(void);

// Keep a node's keystroke: a USB keyboard payload from its mode byte on
// (keyboard_payload_valid checks the whole payload). False if there are too many nodes.
bool keyboard_add(uint16_t id, const uint8_t *stroke, uint8_t len);

// Type a node's keystroke. It is dropped if the PC isn't listening, or if
// KEYBOARD_QUEUE keystrokes are already waiting.
void keyboard_press(uint16_t id);

// Keep typing; call it from core 0's main loop. True while keystrokes are waiting.
bool keyboard_task(void);

#endif
