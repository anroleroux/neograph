#include <math.h>

#include "stepper.h"

// The half-step cycle, IN1 = bit 0: one coil, then it and the next, and so on. Its
// even steps are the wave cycle, its odd ones the full-step cycle.
static const uint8_t HALF_CYCLE[8] = { 0x1, 0x3, 0x2, 0x6, 0x4, 0xC, 0x8, 0x9 };

// The furthest a target may be, in steps, so a float holds it to the step.
#define TARGET_MAX 8000000.0f

stepper_motion stepper_motion_of(const prog_node *n) {
    float steps_rev = (float)n->steps_rev * ((n->options & STEPPER_DRIVE_MASK) == STEPPER_HALF ? 2 : 1);
    float per_rpm = steps_rev / 60;     // steps per second at 1 RPM
    float rate = n->rpm * per_rpm;
    return (stepper_motion){
        .steps_rev = steps_rev,
        .rate_max = rate > STEPPER_RATE_MAX ? STEPPER_RATE_MAX : rate,
        .accel = n->accel * per_rpm,
    };
}

int32_t stepper_target(const prog_node *n, const stepper_motion *m, float in) {
    float t = in * n->turn_scale * m->steps_rev;
    if (!(t - t == 0)) t = 0;           // not finite
    t = t > TARGET_MAX ? TARGET_MAX : t < -TARGET_MAX ? -TARGET_MAX : t;
    return (int32_t)lroundf(t);
}

// The speed its input asks for, steps per second: its input × RPM per unit, within its
// top speed.
static float wanted_speed(const prog_node *n, const stepper_motion *m, float in) {
    float v = in * n->turn_scale * m->steps_rev / 60;
    if (!(v - v == 0)) return 0;
    return v > m->rate_max ? m->rate_max : v < -m->rate_max ? -m->rate_max : v;
}

// The slowest speed it may stop from at once: what one step of its acceleration
// gives from still (any speed, with no acceleration).
static float stop_speed(const stepper_motion *m) {
    return m->accel > 0 ? sqrtf(2 * m->accel) : INFINITY;
}

bool stepper_settled(const prog_node *n, const stepper_motion *m, const stepper_state *st, float in) {
    if (st->speed != 0) return false;
    if (n->options & STEPPER_SPEED) return wanted_speed(n, m, in) == 0;
    return stepper_target(n, m, in) == st->pos;
}

int stepper_advance(const prog_node *n, const stepper_motion *m, stepper_state *st, float in, float dt) {
    float goal;
    if (n->options & STEPPER_SPEED) {
        goal = wanted_speed(n, m, in);
        if (goal == 0 && fabsf(st->speed) <= (m->accel > 0 ? m->accel * dt : INFINITY)) {
            st->speed = st->frac = 0;
            st->moving = false;
            return 0;
        }
    } else {
        // Head for the target, no faster than it can stop from in the steps left:
        // v² = 2 a d.
        int32_t target = stepper_target(n, m, in);
        float d = (float)(target - st->pos) - st->frac;
        if (target == st->pos && fabsf(st->speed) <= stop_speed(m)) {
            st->speed = st->frac = 0;
            st->moving = false;
            return 0;
        }
        float v = m->accel > 0 ? sqrtf(2 * m->accel * fabsf(d)) : m->rate_max;
        if (v > m->rate_max) v = m->rate_max;
        goal = d > 0 ? v : -v;
    }

    // Towards the goal, by its acceleration.
    if (m->accel > 0) {
        float most = m->accel * dt;
        st->speed += goal > st->speed + most ? most : goal < st->speed - most ? -most : goal - st->speed;
    } else {
        st->speed = goal;
    }
    st->moving = true;

    st->frac += st->speed * dt;
    int step = st->frac >= 1 ? 1 : st->frac <= -1 ? -1 : 0;
    if (step) {
        st->pos += step;
        st->frac -= step;
        // A late call (a long scan) takes one step, not several at once.
        st->frac = st->frac > 0.5f ? 0.5f : st->frac < -0.5f ? -0.5f : st->frac;
        // Turning on and on at a speed, it keeps its place in the coils' cycle only.
        if (st->pos > 0x40000000 || st->pos < -0x40000000) st->pos %= 8;
    }
    return step;
}

float stepper_wait(const stepper_state *st) {
    float v = fabsf(st->speed);
    if (v == 0) return STEPPER_TICK_S;
    float left = st->speed > 0 ? 1 - st->frac : 1 + st->frac;
    float t = left / v;
    return t < STEPPER_TICK_S ? t : STEPPER_TICK_S;
}

uint8_t stepper_coils(const prog_node *n, int32_t pos) {
    uint32_t p = (uint32_t)(n->options & STEPPER_REVERSE ? -pos : pos);
    switch (n->options & STEPPER_DRIVE_MASK) {
    case STEPPER_HALF: return HALF_CYCLE[p & 7];
    case STEPPER_FULL: return HALF_CYCLE[(2 * p + 1) & 7];
    default: return HALF_CYCLE[(2 * p) & 7];
    }
}
