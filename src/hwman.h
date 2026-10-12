#ifndef HWMAN_H_INCLUDED
#define HWMAN_H_INCLUDED

#include <stdbool.h>

void core1_worker();

// Pause core 1 so core 0 can write flash. Core 1 executes from flash, so it must
// not run while flash is erased or programmed. core1_pause waits (up to 1 s) until
// core 1 is parked in RAM with interrupts off; it returns false if core 1 did not
// park in time, and then flash must not be written. Before core 1 is launched,
// core1_pause returns true at once. Always pair a successful pause with
// core1_resume.
bool core1_pause(void);
void core1_resume(void);

#endif
