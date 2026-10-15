#ifndef HWMAN_H_INCLUDED
#define HWMAN_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

void core1_worker();

// Pause core 1 so core 0 can write flash. Core 1 executes from flash, so it must
// not run while flash is erased or programmed. core1_pause waits (up to 1 s) until
// core 1 is parked in RAM with interrupts off; it returns false if core 1 did not
// park in time, and then flash must not be written. Before core 1 is launched,
// core1_pause returns true at once. Always pair a successful pause with
// core1_resume.
bool core1_pause(void);
void core1_resume(void);

// Core 0's side of the inter-core FIFO. Core 0 sends every word to core 1 with
// push_to_core1, and takes core 1's messages (a tag and a value) with
// take_from_core1, which returns false when none is waiting. While push_to_core1
// waits for room, it keeps taking core 1's messages for take_from_core1: core 1
// may be waiting for room to send, and would never take core 0's word.
void push_to_core1(uint32_t word);
bool take_from_core1(uint32_t *tag, uint32_t *value);

#endif
