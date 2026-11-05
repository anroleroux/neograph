#include <string.h>

#include "neopixel.h"

float neopixel_fraction(const prog_node *n, float in) {
    float full = n->in_full[0];
    float f = full > 0 ? in / full : 0;
    if (!(f - f == 0)) f = 0;           // not finite
    return f < 0 ? 0 : f > 1 ? 1 : f;
}

bool neopixel_enabled(const prog_node *n, float enable) {
    return n->in[1] == PROG_NO_SIGNAL || enable >= 0.5f;
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

// Its colour for its input at `fraction`: the one colour, or with
// NEOPIXEL_GFX_COLOUR_INPUT from its colour at 0 to its colour at 1. Scaled by an
// amount, 0 to 1, and the node's brightness; as a WS2812 takes it.
static uint32_t colour(const neopixel_payload *px, float fraction, float amount) {
    float t = px->gfx_options & NEOPIXEL_GFX_COLOUR_INPUT ? fraction : 0;
    float k = amount * px->brightness / 255.0f;
    uint32_t c[3];
    for (int i = 0; i < 3; i++) {
        float v = px->colour[0][i] + ((float)px->colour[1][i] - px->colour[0][i]) * t;
        c[i] = (uint32_t)(v * k + 0.5f);
    }
    return c[1] << 24 | c[0] << 16 | c[2] << 8;
}

// Light step `e` of a bar, from the start of the bar.
static void bar_light(const neopixel_payload *px, uint16_t e, uint32_t c, uint32_t *out) {
    bool reverse = px->gfx_options & NEOPIXEL_GFX_REVERSE;
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

// A circle's (or doughnut's) place: its rank from the centre out, and its angle, in
// turns from the ring's LED 0, in wiring order.
static void ring_place(const neopixel_payload *px, const uint8_t at[2], const uint8_t outwards[NEOPIXEL_RINGS_MAX],
                       float *rank, float *turn) {
    uint8_t k = px->layout == NEOPIXEL_CIRCLE ? px->size_len : 1;
    for (uint8_t j = 0; j < k; j++) if (outwards[j] == at[0]) *rank = j;
    *turn = (float)at[1] / px->size[at[0]];
}

// How far round a point on a circle or doughnut goes, in turns: from its start angle
// to its end angle in wiring order, or reversed the other way; once round from an LED
// to itself.
static float point_sweep(const neopixel_payload *px, float turn0, float turn1) {
    float sweep = px->gfx_options & NEOPIXEL_GFX_REVERSE ? turn0 - turn1 : turn1 - turn0;
    if (sweep < 0) sweep += 1;
    if (sweep == 0 && px->from[0] == px->to[0] && px->from[1] == px->to[1]) sweep = 1;
    return sweep;
}

uint16_t neopixel_point_led(const neopixel_payload *px, float fraction) {
    if (px->layout == NEOPIXEL_ARRAY) {
        // Straight from its start to its end: across, up or down, or on a slant.
        float row = px->from[0] + ((float)px->to[0] - px->from[0]) * fraction;
        float col = px->from[1] + ((float)px->to[1] - px->from[1]) * fraction;
        return neopixel_array_led(px, (uint8_t)(row + 0.5f), (uint8_t)(col + 0.5f));
    }
    // A circle or doughnut: from its start ring to its end ring, and around from its
    // start angle to its end angle, in wiring order (reversed, the other way). The same
    // LED at both ends goes once all the way round.
    uint8_t outwards[NEOPIXEL_RINGS_MAX] = { 0 };
    if (px->layout == NEOPIXEL_CIRCLE) rings_outwards(px, outwards);
    float rank0 = 0, rank1 = 0, turn0, turn1;
    ring_place(px, px->from, outwards, &rank0, &turn0);
    ring_place(px, px->to, outwards, &rank1, &turn1);
    bool reverse = px->gfx_options & NEOPIXEL_GFX_REVERSE;
    float sweep = point_sweep(px, turn0, turn1);
    float turn = turn0 + (reverse ? -sweep : sweep) * fraction;
    turn -= (float)(int)turn;
    if (turn < 0) turn += 1;
    uint8_t ring = outwards[(uint8_t)(rank0 + (rank1 - rank0) * fraction + 0.5f)];
    uint16_t n = px->size[ring];
    return (uint16_t)(ring_first(px, ring) + (uint16_t)(turn * n + 0.5f) % n);
}

uint16_t neopixel_point_steps(const neopixel_payload *px) {
    if (px->layout == NEOPIXEL_ARRAY) {
        // Steps of one row or column at a time, whichever it moves more of.
        int rows = px->to[0] - px->from[0], cols = px->to[1] - px->from[1];
        rows = rows < 0 ? -rows : rows;
        cols = cols < 0 ? -cols : cols;
        return (uint16_t)(rows > cols ? rows : cols);
    }
    // The rings it crosses, or the LEDs it goes around on the bigger of its two rings,
    // whichever is more.
    uint8_t outwards[NEOPIXEL_RINGS_MAX] = { 0 };
    if (px->layout == NEOPIXEL_CIRCLE) rings_outwards(px, outwards);
    float rank0 = 0, rank1 = 0, turn0, turn1;
    ring_place(px, px->from, outwards, &rank0, &turn0);
    ring_place(px, px->to, outwards, &rank1, &turn1);
    float sweep = point_sweep(px, turn0, turn1);
    uint8_t n = px->size[px->from[0]] > px->size[px->to[0]] ? px->size[px->from[0]] : px->size[px->to[0]];
    uint16_t around = (uint16_t)(sweep * n + 0.5f);
    uint16_t across = (uint16_t)(rank1 > rank0 ? rank1 - rank0 : rank0 - rank1);
    return around > across ? around : across;
}

// A point: the two LEDs on its path either side of where it is, each lit as much as it
// is near it; on one of them, that one. Its path is neopixel_point_steps steps, each
// from one LED (neopixel_point_led) to the next.
static void point_light(const neopixel_payload *px, float fraction, uint32_t *out) {
    uint16_t steps = neopixel_point_steps(px);
    if (steps == 0) {
        out[neopixel_point_led(px, fraction)] = colour(px, fraction, 1);
        return;
    }
    float at = fraction * steps;
    uint16_t i = (uint16_t)at;
    if (i >= steps) i = steps - 1;
    float near = at - i;                    // 0 on LED a, 1 on LED b
    uint16_t a = neopixel_point_led(px, (float)i / steps), b = neopixel_point_led(px, (float)(i + 1) / steps);
    if (a == b) {
        out[a] = colour(px, fraction, 1);
        return;
    }
    out[a] = colour(px, fraction, 1 - near);
    out[b] = colour(px, fraction, near);
}

void neopixel_patch_span(const neopixel_payload *px, uint8_t ring, uint8_t *start, uint8_t *more) {
    float rank, turn0, turn1;
    uint8_t outwards[NEOPIXEL_RINGS_MAX] = { 0 };
    if (px->layout == NEOPIXEL_CIRCLE) rings_outwards(px, outwards);
    ring_place(px, px->from, outwards, &rank, &turn0);
    ring_place(px, px->to, outwards, &rank, &turn1);
    float sweep = px->gfx_options & NEOPIXEL_GFX_REVERSE ? turn0 - turn1 : turn1 - turn0;
    if (sweep < 0) sweep += 1;
    uint8_t n = px->size[ring];
    uint16_t m = (uint16_t)(sweep * n + 0.5f);
    *start = (uint8_t)((uint16_t)(turn0 * n + 0.5f) % n);
    *more = (uint8_t)(m < n ? m : n - 1);
}

// A patch: every LED from its start to its end, in its colour (which its input may set).
// On an array the rectangle with those corners; on a circle or doughnut, on each ring
// from its start ring to its end ring, the LEDs from the one nearest its start angle
// round to the one nearest its end angle.
static void patch_light(const neopixel_payload *px, float fraction, uint32_t *out) {
    uint32_t c = colour(px, fraction, 1);
    if (px->layout == NEOPIXEL_ARRAY) {
        uint8_t r0 = px->from[0] < px->to[0] ? px->from[0] : px->to[0], r1 = px->from[0] ^ px->to[0] ^ r0;
        uint8_t c0 = px->from[1] < px->to[1] ? px->from[1] : px->to[1], c1 = px->from[1] ^ px->to[1] ^ c0;
        for (uint16_t r = r0; r <= r1; r++)
            for (uint16_t col = c0; col <= c1; col++) out[neopixel_array_led(px, (uint8_t)r, (uint8_t)col)] = c;
        return;
    }
    uint8_t outwards[NEOPIXEL_RINGS_MAX] = { 0 };
    if (px->layout == NEOPIXEL_CIRCLE) rings_outwards(px, outwards);
    float rank0 = 0, rank1 = 0, turn;
    ring_place(px, px->from, outwards, &rank0, &turn);
    ring_place(px, px->to, outwards, &rank1, &turn);
    uint8_t lo = (uint8_t)(rank0 < rank1 ? rank0 : rank1), hi = (uint8_t)(rank0 < rank1 ? rank1 : rank0);
    bool reverse = px->gfx_options & NEOPIXEL_GFX_REVERSE;
    for (uint8_t j = lo; j <= hi; j++) {
        uint8_t ring = outwards[j], n = px->size[ring], start, more;
        uint16_t first = ring_first(px, ring);
        neopixel_patch_span(px, ring, &start, &more);
        for (uint16_t e = 0; e <= more; e++) out[first + (reverse ? start + n - e : start + e) % n] = c;
    }
}

void neopixel_render(const neopixel_payload *px, float fraction, bool on, uint32_t *out) {
    memset(out, 0, px->leds * sizeof out[0]);
    if (!on) return;
    if (px->graphic == NEOPIXEL_GFX_POINT) {
        point_light(px, fraction, out);
        return;
    }
    if (px->graphic == NEOPIXEL_GFX_PATCH) {
        patch_light(px, fraction, out);
        return;
    }
    // A bar: its steps up to fraction × its length lit, and the step it ends in lit as
    // much as it is reached, so it moves smoothly.
    uint16_t steps = bar_steps(px);
    float lit = fraction * steps;
    for (uint16_t e = 0; e < steps && lit > e; e++) {
        float amount = lit - e;
        bar_light(px, e, colour(px, fraction, amount > 1 ? 1 : amount), out);
    }
}
