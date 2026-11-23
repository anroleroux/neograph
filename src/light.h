#ifndef LIGHT_H_INCLUDED
#define LIGHT_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"

/*
 * A light sensor (MSG_HW_LIGHT) as core 1 reads it: a phototransistor into a sense node
 * on an ADC pin, with load resistors switched in by GPIOs. This is its arithmetic, with
 * no hardware here, so the tests run it too: each range's load, auto-ranging, a reading
 * as a photocurrent, and averaging LED bursts. hwman.c switches the loads, drives the
 * LED and reads the ADC. PROTOCOL.md, "Light sensors", describes it.
 */

#define LIGHT_RAW_HIGH          3900    /* a reading above this is near the top: a less sensitive range */
#define LIGHT_RAW_LOW           200     /* below this, a more sensitive range, if the reading there ... */
#define LIGHT_RAW_TARGET        3000    /* ... would be under this: the gap keeps it from flapping */
#define LIGHT_NO_RANGE          0xFF

// The load range k puts on the sense node, in ohms: its own load in parallel with the
// always-on one (range 0 is the always-on load alone). 0 if it has no range k.
float light_load_ohms(const light_payload *l, uint8_t k);

// Its ranges, most sensitive (the highest load) first, into `order`. Returns how many.
uint8_t light_ranges(const light_payload *l, uint8_t order[LIGHT_RANGES]);

// The range it reads next, auto-ranging, after a reading of `raw` on range `k`: the next
// less sensitive one when `raw` is above LIGHT_RAW_HIGH; the next more sensitive one when
// `raw` is below LIGHT_RAW_LOW and would read under LIGHT_RAW_TARGET there; otherwise `k`.
// A range it doesn't have gives its most sensitive one.
uint8_t light_autorange(const light_payload *l, uint8_t k, uint16_t raw);

// The photocurrent a reading of `raw` (0 to ADC_RAW_FULL, 3.3 V) through `ohms` stands
// for, in µA: I = V / R.
float light_microamps(float raw, float ohms);

// The full scale of its output k, for an output that takes a fraction (a PWM): its µA
// (0), the most its least sensitive range reads; its reflected µA (1), the most its
// reflection range reads; its reading (2), ADC_RAW_FULL; its load (3), in kΩ, its most
// sensitive range's.
float light_full(const light_payload *l, uint8_t k);

// LED bursts averaged into a reflection reading.
typedef struct {
    int32_t sum;            // the bursts' on reading less the mean of their off readings
    uint8_t count;
} light_bursts;

// Add a burst: the reading with the LED off, then on, then off again. Once it has
// `bursts` of them, *counts is their mean reflection (on - mean(off)), in ADC counts,
// the sum starts again, and it returns true.
bool light_burst(light_bursts *b, uint16_t off1, uint16_t on, uint16_t off2, uint8_t bursts, float *counts);

#endif
