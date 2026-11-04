#ifndef STEPPER_H_INCLUDED
#define STEPPER_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

#include "program.h"

/*
 * A stepper motor's motion (MSG_HW_STEPPER), as core 1 runs it: no hardware here, so
 * the tests run it too. Core 1 calls stepper_advance often while the motor moves (at
 * least every STEPPER_TICK_S, and when its next step is due, stepper_wait), and puts
 * stepper_coils of its position on IN1 to IN4 after each step.
 *
 * Positions and speeds are in the steps of its drive: a half-stepped motor has twice
 * the steps of a revolution of a wave- or full-stepped one.
 */

#define STEPPER_TICK_S      0.001f      // the longest core 1 waits between calls while it moves

typedef struct {
    float steps_rev;        // steps a revolution, in its drive
    float rate_max;         // its top speed, steps per second, at most STEPPER_RATE_MAX
    float accel;            // steps per second it gains or loses a second; 0 = at once
} stepper_motion;

typedef struct {
    int32_t pos;            // steps from where it started
    float frac;             // how far it is towards its next step, -1 to 1
    float speed;            // steps per second; below 0 it turns backwards
    bool moving;            // core 1 is to call stepper_advance
} stepper_state;

// A stepper node's motion, from its settings.
stepper_motion stepper_motion_of(const prog_node *n);

// The step its input asks for: where it is to be (its input × revolutions per unit),
// rounded to a step.
int32_t stepper_target(const prog_node *n, const stepper_motion *m, float in);

// Whether it has nothing to do for this input: still, and where its input puts it (or
// with STEPPER_SPEED, its input asks for no speed).
bool stepper_settled(const prog_node *n, const stepper_motion *m, const stepper_state *st, float in);

// dt seconds on: its speed towards the one its input asks for, within its acceleration
// and top speed, and slowing in time to stop where its input puts it. Returns +1 or -1
// when that takes it a step on or back (st->pos has moved), 0 otherwise. Clears
// st->moving once it is settled.
int stepper_advance(const prog_node *n, const stepper_motion *m, stepper_state *st, float in, float dt);

// Seconds until stepper_advance is next needed: its next step at this speed, or
// STEPPER_TICK_S if that is sooner.
float stepper_wait(const stepper_state *st);

// Which coils are on at a position, bit k for IN(k + 1), in its drive and direction.
uint8_t stepper_coils(const prog_node *n, int32_t pos);

#endif
