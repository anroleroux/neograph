#ifndef ULTRASONIC_H_INCLUDED
#define ULTRASONIC_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"

/*
 * An ultrasonic ranger (MSG_HW_ULTRASONIC), such as Seeed's Grove Ultrasonic Ranger, as
 * core 1 reads it: the arithmetic of an echo and what a ping's outcome makes of its
 * outputs, with no hardware here, so the tests run it too. hwman.c drives the pin and
 * times the echo. PROTOCOL.md, "Ultrasonic rangers", describes it.
 */

#define ULTRASONIC_TRIGGER_US   10      /* the pulse on its pin that starts a ping */
#define ULTRASONIC_PING_MS      60      /* from one ping to the next: the last one's echoes have died away */
#define ULTRASONIC_PULSE_MIN_US 100     /* a shorter echo pulse, under 2 cm, is noise */
#define ULTRASONIC_MISSES_LOST  3       /* pings in a row without an echo before echo goes off */

// The distance an echo pulse of `pulse_us` stands for, in cm, to 0.1 cm: sound goes
// there and back at 343 m/s (in air at 20 °C), so 58.3 µs a cm.
float ultrasonic_cm(uint32_t pulse_us);

// What a ping makes of its outputs. `answered`: the ranger gave an echo pulse, of
// `pulse_us`. An echo from within `range_cm` sets *cm, turns *echo on and clears
// *misses. Anything else (no pulse, one too short, one from beyond its range) is a
// miss: after ULTRASONIC_MISSES_LOST in a row *echo goes off and *cm goes to its range,
// as far as it sees; until then they keep what they had, so one lost ping doesn't
// show.
void ultrasonic_take(bool answered, uint32_t pulse_us, uint16_t range_cm, float *cm, bool *echo, uint8_t *misses);

#endif
