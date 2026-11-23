#include "light.h"

float light_load_ohms(const light_payload *l, uint8_t k) {
    if (!light_has_range(l, k)) return 0;
    float base = (float)l->ohms[0];
    if (k == 0) return base;
    float own = (float)l->ohms[k];
    return base > 0 ? own * base / (own + base) : own;
}

uint8_t light_ranges(const light_payload *l, uint8_t order[LIGHT_RANGES]) {
    uint8_t n = 0;
    for (uint8_t k = 0; k < LIGHT_RANGES; k++) {
        if (!light_has_range(l, k)) continue;
        // Insert it after every range with a higher load.
        uint8_t at = n;
        while (at > 0 && light_load_ohms(l, order[at - 1]) < light_load_ohms(l, k)) {
            order[at] = order[at - 1];
            at--;
        }
        order[at] = k;
        n++;
    }
    return n;
}

uint8_t light_autorange(const light_payload *l, uint8_t k, uint16_t raw) {
    uint8_t order[LIGHT_RANGES];
    uint8_t n = light_ranges(l, order);
    uint8_t at = 0;
    while (at < n && order[at] != k) at++;
    if (at == n) return order[0];
    if (raw > LIGHT_RAW_HIGH && at + 1 < n) return order[at + 1];
    if (raw < LIGHT_RAW_LOW && at > 0) {
        float there = raw * light_load_ohms(l, order[at - 1]) / light_load_ohms(l, k);
        if (there < LIGHT_RAW_TARGET) return order[at - 1];
    }
    return k;
}

float light_microamps(float raw, float ohms) {
    if (ohms <= 0) return 0;
    return raw * 3.3f / ADC_RAW_FULL / ohms * 1e6f;
}

float light_full(const light_payload *l, uint8_t k) {
    uint8_t order[LIGHT_RANGES];
    uint8_t n = light_ranges(l, order);
    switch (k) {
    case 0: return light_microamps(ADC_RAW_FULL, light_load_ohms(l, order[n - 1]));
    case 1: return light_has_range(l, l->reflect_range) ? light_microamps(ADC_RAW_FULL, light_load_ohms(l, l->reflect_range)) : 1;
    case 2: return ADC_RAW_FULL;
    case 3: return light_load_ohms(l, order[0]) / 1000.0f;
    default: return 1;                  // its LED: on or off
    }
}

bool light_reflect_take(light_reflect *r, uint16_t raw, uint8_t bursts, float *counts) {
    bool done = false;
    if (r->led) {
        // On: kept if an off reading came before it.
        if (r->have_off) {
            r->on = raw;
            r->have_on = true;
        }
        r->led = false;
        return false;
    }
    if (r->have_on) {
        r->sum += 2 * (int32_t)r->on - r->off - raw;
        r->have_on = false;
        if (++r->count >= bursts) {
            *counts = (float)r->sum / (2.0f * r->count);
            r->sum = 0;
            r->count = 0;
            done = true;
        }
    }
    r->off = raw;
    r->have_off = true;
    r->led = true;
    return done;
}
