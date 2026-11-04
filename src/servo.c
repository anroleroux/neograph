#include "servo.h"

float servo_target(const prog_node *n, float in) {
    float full = n->in_full[0];
    float f = full > 0 ? in / full : 0;
    if (!(f - f == 0)) f = 0;           // not finite
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    return n->angle_min + ((float)n->angle_max - n->angle_min) * f;
}

float servo_move(const prog_node *n, float angle, float target, float dt) {
    if (n->slew == 0) return target;
    float most = n->slew * dt;
    return target > angle + most ? angle + most : target < angle - most ? angle - most : target;
}

float servo_pulse_us(const prog_node *n, float angle) {
    return n->pulse_min + ((float)n->pulse_max - n->pulse_min) * angle / SERVO_ANGLE_MAX;
}
