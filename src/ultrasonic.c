#include <math.h>

#include "ultrasonic.h"

float ultrasonic_cm(uint32_t pulse_us) {
    return roundf((float)pulse_us * 0.1715f) / 10.0f;      // 0.01715 cm a µs, there and back halved
}

void ultrasonic_take(bool answered, uint32_t pulse_us, uint16_t range_cm, float *cm, bool *echo, uint8_t *misses) {
    float at = answered ? ultrasonic_cm(pulse_us) : 0;
    if (answered && pulse_us >= ULTRASONIC_PULSE_MIN_US && at <= (float)range_cm) {
        *cm = at;
        *echo = true;
        *misses = 0;
        return;
    }
    if (*misses < ULTRASONIC_MISSES_LOST) ++*misses;
    if (*misses < ULTRASONIC_MISSES_LOST) return;
    *echo = false;
    *cm = (float)range_cm;
}
