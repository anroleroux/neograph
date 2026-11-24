#ifndef LIGHT_H_INCLUDED
#define LIGHT_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"

/*
 * A light sensor (MSG_HW_LIGHT) as core 1 reads it: a phototransistor into a sense node
 * on an ADC pin, with load resistors switched in by GPIOs. This is its arithmetic, with
 * no hardware here, so the tests run it too: each range's load, auto-ranging, a reading
 * as a photocurrent, its LEDs' on and off scans for reflections, and an object's
 * direction. hwman.c switches the loads and reads the ADC; its LEDs are whatever its LED
 * outputs are wired to.
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

// The full scale of its output k, for an output that takes a fraction (a PWM): its
// background µA, the most its least sensitive range reads; a reflected µA (the object's,
// left or right), the most its reflection range reads; its direction, and its LEDs, 1.
float light_full(const light_payload *l, uint8_t k);

// Its LEDs: the object's, then the left and right ones, each a channel of reflections.
#define LIGHT_LED_OBJECT        0
#define LIGHT_LED_LEFT          1
#define LIGHT_LED_RIGHT         2
#define LIGHT_LEDS              3
#define LIGHT_DARK              0xFF    /* no LED lit */

// Reflections: one LED on for a scan, between scans with them all off, the LEDs in turn.
// Each scan reads the reflection range with the LEDs as the scan before set them (its
// outputs run after it, so they have been that way for about a scan). A burst is an off
// reading, an on reading and the next off reading, which also starts the next burst.
typedef struct {
    uint8_t lit;            // the LED it lit at the last scan, or LIGHT_DARK
    uint8_t next;           // the LED it lights next
    bool have_off, have_on; // `off` holds a reading with them off; `on` one with `on_led` lit, after `off`
    uint8_t on_led;
    uint16_t off, on;
    int32_t sum[LIGHT_LEDS];    // twice each burst's reflection, since its last result
    uint8_t count[LIGHT_LEDS];
} light_reflect;

// Take this scan's reading on the reflection range, made with r->lit lit. An off reading
// after an off and an on one ends that LED's burst: on less the mean of the two offs.
// Once an LED has `bursts` of them, counts[led] is their mean, in ADC counts, and its bit
// is set in the result. Afterwards r->lit is the LED to light until the next scan: after
// an on reading none; after an off one the next of the first `leds` LEDs (1: the object's
// only; LIGHT_LEDS: the object's, left, right). The bursts of an LED past `leds` are dropped.
uint8_t light_reflect_take(light_reflect *r, uint16_t raw, uint8_t leds, uint8_t bursts, float counts[LIGHT_LEDS]);

// Its pace: from the start of one reflection reading to the next, `every` scans (0: one
// after another), resting in between with its LEDs off.
typedef struct {
    bool resting;
    uint32_t since;         // scans done since its reflection reading began
} light_pace;

// At the start of a scan: whether it measures reflections this scan, or rests.
bool light_pace_measure(light_pace *p, uint32_t every);

// After a scan it measured reflections in, `done` if its reading is in (its last LED's).
// Returns true if it now rests until the next is due (the caller puts its LEDs out with
// light_reflect_rest).
bool light_pace_rest(light_pace *p, uint32_t every, bool done);

// Rest between reflection readings: its LEDs off, and the next reading starts afresh, from
// an off reading and its first LED.
void light_reflect_rest(light_reflect *r);

// An object's direction from its left and right reflections, in µA: (right - left) /
// (right + left), -1 (left) to 1 (right), each taken as 0 if below 0. 0 if neither
// reaches `min_ua`: nothing there.
float light_direction(float left, float right, float min_ua);

#endif
