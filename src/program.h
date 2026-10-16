#ifndef PROGRAM_H_INCLUDED
#define PROGRAM_H_INCLUDED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol.h"

/*
 * The program core 1 runs: a config's graph, compiled by core 0 into shared RAM
 * (PROTOCOL.md, "Part 3"). It has
 *
 *   - signals: the value of each output of each source (a button, a slider's
 *     value, a 2D slider's X and Y, an ADC, a GPIO input, the core temperature,
 *     a logic element), as a float in the source's own unit;
 *   - nodes: the elements core 1 runs, in the order it runs them: the hardware
 *     inputs (ADC, GPIO input, core temperature), which write a signal; then the
 *     logic (compare, AND, OR, NOT, delay), which reads signals and writes its
 *     own, each after the logic it reads; then the outputs (LED, GPO, PWM, USB
 *     keyboard), which read one or two.
 *
 * Core 1 evaluates the nodes in order every scan. Only the signals' values change
 * while it runs a program; core 0 reads them for telemetry.
 */

#define PROG_SIGNALS_MAX    96
#define PROG_NODES_MAX      64
#define PROG_NODE_INPUTS    LOGIC_INPUTS_MAX    /* the most signals a node reads: an AND's or OR's */
#define PROG_NO_SIGNAL      0xFF    /* a node input wired to nothing: it reads 0 */
#define PROG_ADC_VOLTS      3.3f    /* an ADC signal at ADC_RAW_FULL */

typedef struct {
    uint16_t id;            // the source element
    uint8_t output;         // which of its outputs
    uint8_t type;           // its element type (MSG_)
    volatile float value;   // core 1 writes it; core 0 reads it for telemetry
} prog_signal;

typedef struct {
    uint8_t type;           // MSG_HW_ or MSG_LOGIC_
    uint16_t id;
    uint8_t pin;            // GPO, PWM, GPIO input: its GPIO; ADC: its channel
    uint8_t options;        // GPIO input: GPI_ options; PWM: PWM_ options; compare: COMPARE_ABOVE or _BELOW
    uint8_t out;            // an input or logic: the signal it writes
    uint8_t in_count;       // the signals it reads
    uint8_t in[PROG_NODE_INPUTS];   // logic or an output: the signals it reads; a PWM's duty, then its frequency
    float in_full[2];       // a PWM: the value of each input that gives its max (a slider's 1, an ADC's 3.3 V)
    uint8_t duty, duty_min, duty_max;       // a PWM, percent
    uint16_t freq, freq_min, freq_max;      // a PWM, Hz
    float threshold, hysteresis;            // a compare
    uint32_t on_ms, off_ms;                 // a delay
} prog_node;

typedef struct {
    uint8_t signal_count;
    uint8_t node_count;
    prog_signal signals[PROG_SIGNALS_MAX];
    prog_node nodes[PROG_NODES_MAX];
} hw_program;

// Compile a config's payload (its messages, after the packet header) into a
// program. Messages the device doesn't run (lights, plots, labels, networking)
// and malformed ones are left out, as are signals and nodes over the limits.
void program_compile(const uint8_t *payload, size_t len, hw_program *out);

// The signal of an output of a source; PROG_NO_SIGNAL if the program has none.
uint8_t program_signal(const hw_program *p, uint16_t id, uint8_t output);

// Whether a type is a logic element's (MSG_LOGIC_).
bool program_logic(uint8_t type);

// A logic node's state after one scan of dt_ms, from its state before (on), as
// core 1 runs it (PROTOCOL.md, "Logic"). held_ms is a delay's count of how long
// its input has differed from its state; it starts at 0.
bool program_logic_step(const hw_program *p, const prog_node *n, bool on, uint32_t *held_ms, uint32_t dt_ms);

// Whether a source of this type is operated from the browser (a button, a
// slider, a 2D slider): its signal is set by commands.
bool program_ui_source(uint8_t type);

// The program as text, one signal or node a line, as in PROTOCOL.md's `program`
// examples. Returns the length it would have; writes at most size - 1 bytes.
size_t program_describe(const hw_program *p, char *buf, size_t size);

#endif
