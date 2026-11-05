#include <string.h>

#include "neopixel.h"

float neopixel_fraction(const prog_node *n, float in) {
    float full = n->in_full[0];
    float f = full > 0 ? in / full : 0;
    if (!(f - f == 0)) f = 0;           // not finite
    return f < 0 ? 0 : f > 1 ? 1 : f;
}

uint16_t neopixel_array_led(const neopixel_payload *px, uint8_t row, uint8_t col) {
    uint8_t cols = px->size[1];
    if ((px->options & NEOPIXEL_ZIGZAG) && (row & 1)) col = (uint8_t)(cols - 1 - col);
    return (uint16_t)(row * cols + col);
}

// The first LED of a circle's ring.
static uint16_t ring_first(const neopixel_payload *px, uint8_t ring) {
    uint16_t first = 0;
    for (uint8_t i = 0; i < ring; i++) first += px->size[i];
    return first;
}

// A circle's rings from the centre out: smallest first, and in their wiring order
// among rings of the same size. Returns how many.
static uint8_t rings_outwards(const neopixel_payload *px, uint8_t order[NEOPIXEL_RINGS_MAX]) {
    uint8_t k = px->size_len;
    for (uint8_t i = 0; i < k; i++) order[i] = i;
    for (uint8_t i = 1; i < k; i++) {               // an insertion sort: stable
        uint8_t ring = order[i], j = i;
        for (; j > 0 && px->size[order[j - 1]] > px->size[ring]; j--) order[j] = order[j - 1];
        order[j] = ring;
    }
    return k;
}

// The steps of a bar: the LEDs of a row, column or ring, or a circle's rings.
static uint16_t bar_steps(const neopixel_payload *px) {
    switch (px->layout) {
    case NEOPIXEL_ARRAY: return px->along == NEOPIXEL_BAR_ROW ? px->size[1] : px->size[0];
    case NEOPIXEL_CIRCLE: return px->along == NEOPIXEL_BAR_ROW ? px->size[px->position] : px->size_len;
    default: return px->size[0];
    }
}

// Scale a colour by an amount, 0 to 1, and the node's brightness; as a WS2812 takes it.
static uint32_t colour(const neopixel_payload *px, float amount) {
    float k = amount * px->brightness / 255.0f;
    uint32_t r = (uint32_t)(px->red * k + 0.5f), g = (uint32_t)(px->green * k + 0.5f), b = (uint32_t)(px->blue * k + 0.5f);
    return g << 24 | r << 16 | b << 8;
}

// Light step `e` of a bar, from the start of the bar.
static void bar_light(const neopixel_payload *px, uint16_t e, uint32_t c, uint32_t *out) {
    bool reverse = px->bar_options & NEOPIXEL_BAR_REVERSE;
    uint16_t steps = bar_steps(px);
    uint16_t s = reverse ? (uint16_t)(steps - 1 - e) : e;
    switch (px->layout) {
    case NEOPIXEL_ARRAY:
        // Along a row from the left; up a column from the bottom.
        if (px->along == NEOPIXEL_BAR_ROW) out[neopixel_array_led(px, px->position, (uint8_t)s)] = c;
        else out[neopixel_array_led(px, (uint8_t)(steps - 1 - s), px->position)] = c;
        break;
    case NEOPIXEL_CIRCLE:
        if (px->along == NEOPIXEL_BAR_ROW) {
            // Around the ring from its first LED, as a doughnut.
            uint16_t n = px->size[px->position];
            out[ring_first(px, px->position) + (reverse ? n - e : e) % n] = c;
        } else {
            uint8_t order[NEOPIXEL_RINGS_MAX];
            rings_outwards(px, order);
            uint8_t ring = order[s];
            uint16_t first = ring_first(px, ring);
            for (uint16_t i = 0; i < px->size[ring]; i++) out[first + i] = c;
        }
        break;
    default: {
        // Around from its first LED, in wiring order; reversed, the other way round.
        uint16_t n = px->size[0];
        out[(px->position + (reverse ? n - e : e)) % n] = c;
        break;
    }
    }
}

void neopixel_render(const neopixel_payload *px, float fraction, uint32_t *out) {
    memset(out, 0, px->leds * sizeof out[0]);
    if (px->graphic != NEOPIXEL_GFX_BAR) return;
    // A bar: its steps up to fraction × its length lit, and the step it ends in lit as
    // much as it is reached, so it moves smoothly.
    uint16_t steps = bar_steps(px);
    float lit = fraction * steps;
    for (uint16_t e = 0; e < steps && lit > e; e++) {
        float amount = lit - e;
        bar_light(px, e, colour(px, amount > 1 ? 1 : amount), out);
    }
}
