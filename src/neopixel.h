#ifndef NEOPIXEL_H_INCLUDED
#define NEOPIXEL_H_INCLUDED

#include <stdint.h>

#include "program.h"

/*
 * What a NeoPixel node (MSG_HW_NEOPIXEL) shows, as core 1 works it out each scan: no
 * hardware here, so the tests run it too. hwman.c sends the LEDs' colours out on its pin.
 */

// Its input as a fraction of its full scale (n->in_full[0]), 0 to 1: as a servo or PWM
// takes it. Not a finite number reads 0.
float neopixel_fraction(const prog_node *n, float in);

// Each of its LEDs' colours, in the order they are wired, for its input at `fraction`
// of full scale: out[i] for LED i, of px->leds. A colour is as a WS2812 takes it, and
// as the PIO shifts it out, from bit 31: green in bits 31-24, red 23-16, blue 15-8.
// Its brightness scales each colour.
void neopixel_render(const neopixel_payload *px, float fraction, uint32_t *out);

// The LED an array's row and column (from 0, at the top left) is wired to.
uint16_t neopixel_array_led(const neopixel_payload *px, uint8_t row, uint8_t col);

#endif
