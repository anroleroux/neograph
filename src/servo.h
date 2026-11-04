#ifndef SERVO_H_INCLUDED
#define SERVO_H_INCLUDED

#include <stdint.h>

#include "program.h"

/*
 * A servo's angle and pulse (MSG_HW_SERVO), as core 1 runs it each scan: no
 * hardware here, so the tests run it too.
 */

// The angle its input asks for, in degrees: from its angle at 0 to its angle at 1 as
// its input goes from 0 to its full scale (n->in_full[0]), clamped.
float servo_target(const prog_node *n, float in);

// Its angle dt seconds on from `angle`, towards `target` at its speed limit; at
// once without one.
float servo_move(const prog_node *n, float angle, float target, float dt);

// The pulse for an angle, in µs: from its pulse at 0° to its pulse at SERVO_ANGLE_MAX°.
float servo_pulse_us(const prog_node *n, float angle);

#endif
