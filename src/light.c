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
    case LIGHT_OUT_BACKGROUND: return light_microamps(ADC_RAW_FULL, light_load_ohms(l, order[n - 1]));
    case LIGHT_OUT_REFLECTED:
    case LIGHT_OUT_LEFT:
    case LIGHT_OUT_RIGHT:
        return light_has_range(l, l->reflect_range) ? light_microamps(ADC_RAW_FULL, light_load_ohms(l, l->reflect_range)) : 1;
    default: return 1;                  // its direction; an LED: on or off
    }
}

uint8_t light_reflect_take(light_reflect *r, uint16_t raw, uint8_t leds, uint8_t bursts, float counts[LIGHT_LEDS]) {
    if (r->lit != LIGHT_DARK) {
        // On: kept if an off reading came before it.
        if (r->have_off) {
            r->on = raw;
            r->on_led = r->lit;
            r->have_on = true;
        }
        r->lit = LIGHT_DARK;
        return 0;
    }
    uint8_t ready = 0;
    if (r->have_on) {
        uint8_t k = r->on_led;
        r->sum[k] += 2 * (int32_t)r->on - r->off - raw;
        r->have_on = false;
        if (++r->count[k] >= bursts) {
            counts[k] = (float)r->sum[k] / (2.0f * r->count[k]);
            r->sum[k] = 0;
            r->count[k] = 0;
            ready = (uint8_t)(1u << k);
        }
    }
    for (uint8_t k = leds; k < LIGHT_LEDS; k++) r->sum[k] = r->count[k] = 0;
    r->off = raw;
    r->have_off = true;
    if (r->next >= leds) r->next = 0;
    r->lit = r->next++;
    return ready;
}

bool light_pace_measure(light_pace *p, uint32_t every) {
    if (p->resting) {
        if (p->since < every) {
            p->since++;
            return false;
        }
        p->resting = false;
        p->since = 0;
    }
    return true;
}

bool light_pace_rest(light_pace *p, uint32_t every, bool done) {
    ++p->since;
    if (!every || !done) return false;
    if (p->since >= every) {        // it took as long as its time: the next starts at once
        p->since = 0;
        return false;
    }
    p->resting = true;
    return true;
}

void light_reflect_rest(light_reflect *r) {
    *r = (light_reflect){ .lit = LIGHT_DARK };
}

float light_direction(float left, float right, float min_ua) {
    if (left < 0) left = 0;
    if (right < 0) right = 0;
    if (left < min_ua && right < min_ua) return 0;
    if (left + right <= 0) return 0;
    return (right - left) / (right + left);
}
