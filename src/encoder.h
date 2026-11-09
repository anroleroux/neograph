#ifndef ENCODER_H_INCLUDED
#define ENCODER_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"

/*
 * A rotary encoder (MSG_HW_ENCODER), as core 1 reads it: decoding its two pins into
 * clicks, and moving its position, with no hardware here, so the tests run it too.
 * hwman.c takes its pins' edges. PROTOCOL.md, "Rotary encoders", describes it.
 */

// What the decoder keeps between changes of its pins.
typedef struct {
    uint8_t ab;         // its pins at the last change: A in bit 1, B in bit 0
    int8_t moved;       // transitions since it last rested, + one way, - the other
} encoder_decoder;

// A decoder for an encoder whose pins read `ab` now (A in bit 1, B in bit 0).
void encoder_start(encoder_decoder *d, uint8_t ab);

// Its pins changed, to `ab`: the clicks that makes, -1, 0 or +1, with `detent` its
// transitions a click (ENCODER_DETENT_). A pin's change one way is +1 (A falling
// first from rest, clockwise on a KY-040), back is -1, and both pins at once, a change
// it missed, 0. With ENCODER_DETENT_1 each transition is a click; otherwise it clicks
// when it rests again (both pins high, or with ENCODER_DETENT_2 both low too), having
// gone at least half a click's transitions one way, so a bouncing contact or a missed
// edge doesn't count.
int8_t encoder_decode(encoder_decoder *d, uint8_t ab, uint8_t detent);

// A position moved by `clicks`: clamped to 0 to `steps`, or with `wrap` taken round
// from 0 to steps - 1.
int32_t encoder_position(int32_t position, int32_t clicks, uint16_t steps, bool wrap);

#endif
