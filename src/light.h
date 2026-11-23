#ifndef LIGHT_H_INCLUDED
#define LIGHT_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"

/*
 * A light sensor (MSG_HW_LIGHT) as core 1 reads it: a phototransistor into a sense node
 * on an ADC pin, with load resistors switched in by GPIOs. This is its arithmetic, with
 * no hardware here, so the tests run it too: each range's load, auto-ranging, a reading
 * as a photocurrent, and the LED's on and off scans for reflections. hwman.c switches
 * the loads and reads the ADC; the LED is whatever its LED output is wired to.
 * PROTOCOL.md, "Light sensors", describes it.
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
// sensitive range's; its LED (4), 1.
float light_full(const light_payload *l, uint8_t k);

// Reflections: its LED output on for a scan, between scans with it off. Each scan reads
// the reflection range with the LED as the scan before set it (its outputs run after it,
// so the LED has been that way for about a scan). A burst is an off reading, an on reading
// and the next off reading, which also starts the next burst: so a burst every 2 scans.
typedef struct {
    bool led;               // what its LED output was set to at the last scan
    bool have_off, have_on; // `off` holds a reading with it off; `on` one with it on, after `off`
    uint16_t off, on;
    int32_t sum;            // twice each burst's reflection, since the last result
    uint8_t count;
} light_reflect;

// Take this scan's reading on the reflection range, made with its LED as r->led says. An
// off reading after an off and an on one ends a burst: on less the mean of the two offs.
// Once `bursts` of them are in, *counts is their mean, in ADC counts, the sum starts
// again, and it returns true. Afterwards r->led is what its LED output is to be until
// the next scan: on after an off reading, off after an on one.
bool light_reflect_take(light_reflect *r, uint16_t raw, uint8_t bursts, float *counts);

#endif
