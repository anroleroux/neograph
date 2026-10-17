#include <math.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

#include "hwman.h"
#include "program.h"
#include "protocol.h"

// Core 1 runs the program core 0 compiled from the config (src/program.h): every
// SCAN_MS it reads the hardware inputs into their signals, then drives each
// output from the signals it reads, in the program's order. Commands from the
// browser set the signals of UI sources between scans. PROTOCOL.md, "Part 3",
// describes it.
#define SCAN_MS             10

// ── Core temperature ──
// The chip's temperature is read every TEMP_SAMPLE_SCANS scans, whatever the
// graph: TEMP_AVERAGE reads of the sensor, then smoothed so each sample moves it
// 1/TEMP_FILTER of the way (it follows over about a second, and a sample's noise,
// a sensor count is ~0.5 °C, mostly cancels). The core temperature signals change
// when it has moved TEMP_DEADBAND °C from their value.
#define ADC_TEMP_SENSOR     4       // the temperature sensor is ADC input 4
#define TEMP_AVERAGE        16
#define TEMP_SAMPLE_SCANS   10
#define TEMP_FILTER         8
#define TEMP_DEADBAND       0.1f

static struct {
    bool sampled;
    float celsius;              // smoothed
    float value;                // the signals' value
} core_temp;

// The chip's temperature in °C. The sensor reads 0.706 V at 27 °C and falls
// 1.721 mV per °C (RP2350 datasheet).
static float read_internal_temperature(void) {
    adc_select_input(ADC_TEMP_SENSOR);
    sleep_us(10);  // Allow ADC to stabilize
    uint32_t sum = 0;
    for (int i = 0; i < TEMP_AVERAGE; i++) sum += adc_read();

    float voltage = (sum * 3.3f) / (TEMP_AVERAGE * 4095.0f);
    return 27.0f - ((voltage - 0.706f) / 0.001721f);
}

static void core_temp_sample(void) {
    float sample = read_internal_temperature();
    if (!core_temp.sampled) core_temp.celsius = core_temp.value = sample;
    else core_temp.celsius += (sample - core_temp.celsius) / TEMP_FILTER;
    core_temp.sampled = true;
    if (fabsf(core_temp.celsius - core_temp.value) >= TEMP_DEADBAND) core_temp.value = core_temp.celsius;
}

// ── ADC inputs ──
// Each channel with an ADC node is read every scan: ADC_AVERAGE reads, averaged.
// Its signals, in volts, change when the reading has moved ADC_HYSTERESIS counts,
// so noise doesn't flicker the outputs they drive.
#define ADC_AVERAGE         16
#define ADC_HYSTERESIS      12

static struct {
    bool on;
    bool sampled;
    uint16_t raw;               // the reading the signals hold, 0 to ADC_RAW_FULL
} adc_inputs[ADC_CHANNELS];

static void adc_sample_channels(void) {
    for (uint8_t ch = 0; ch < ADC_CHANNELS; ch++) {
        if (!adc_inputs[ch].on) continue;
        adc_select_input(ch);
        uint32_t sum = 0;
        for (int i = 0; i < ADC_AVERAGE; i++) sum += adc_read();
        uint16_t raw = (sum + ADC_AVERAGE / 2) / ADC_AVERAGE;

        int32_t moved = (int32_t)raw - adc_inputs[ch].raw;
        if (!adc_inputs[ch].sampled || moved >= ADC_HYSTERESIS || -moved >= ADC_HYSTERESIS) {
            adc_inputs[ch].raw = raw;
            adc_inputs[ch].sampled = true;
        }
    }
}

// ── GPIO inputs ──
// Each GPIO input's pin is read every scan. A new state, held for GPI_DEBOUNCE
// reads in a row so a bouncing switch changes it once, becomes its signal: 1 on,
// 0 off.
#define GPI_DEBOUNCE        3

// Read a GPIO input's pin with its input buffer on only for the read. On the
// RP2350's A2 stepping (erratum RP2350-E9), a pad with its input buffer on leaks
// enough current to hold a released pin near mid-rail against the internal
// pull-down, so it would read high long after it was let go. With the buffer
// off between reads, the pull-down brings the pin low.
static bool gpi_read(uint8_t pin) {
    gpio_set_input_enabled(pin, true);
    busy_wait_us_32(1);         // through the input synchronizer
    bool high = gpio_get(pin);
    gpio_set_input_enabled(pin, false);
    return high;
}

// ── PWM outputs ──
// The clock divider is the smallest integer that lets one period fit the 16-bit
// counter: an integer divider keeps the period free of jitter, and the smallest
// one gives the finest duty steps. The slice is shared with one other pin,
// which gets the same frequency. Returns the counter steps in one period.
static uint32_t pwm_set_freq(uint pin, uint16_t freq) {
    if (freq < PWM_FREQ_MIN) freq = PWM_FREQ_MIN;

    uint32_t steps = clock_get_hz(clk_sys) / freq;    // counter steps per period, undivided
    uint32_t div = (steps + 0xFFFE) / 0xFFFF;
    if (div < 1) div = 1;
    if (div > 255) div = 255;
    uint32_t top = steps / div;
    if (top > 0xFFFF) top = 0xFFFF;

    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_set_clkdiv(slice, (float)div);
    pwm_set_wrap(slice, top - 1);
    gpio_set_function(pin, GPIO_FUNC_PWM);
    pwm_set_enabled(slice, true);
    return top;
}

// An input's value as a fraction of the value that gives its max, 0 to 1.
static float pwm_fraction(float value, float full) {
    float f = full > 0 ? value / full : 0;
    return f < 0 ? 0 : f > 1 ? 1 : f;
}

// A PWM's duty cycle, in percent: constant, or from min to max as its input goes
// from 0 to its full value. Min may be above max, which inverts the input.
static float pwm_duty(const prog_node *n, float in) {
    float duty = (n->options & PWM_DUTY_INPUT)
        ? n->duty_min + ((float)n->duty_max - n->duty_min) * pwm_fraction(in, n->in_full[0])
        : n->duty;
    return duty < 0 ? 0 : duty > PWM_DUTY_MAX ? PWM_DUTY_MAX : duty;
}

// A PWM's frequency, in Hz: constant, or from min to max as its input goes from 0
// to its full value, linearly or (PWM_FREQ_LOG) logarithmically: each step of the
// input multiplies it by the same factor.
static uint16_t pwm_freq(const prog_node *n, float in) {
    if (!(n->options & PWM_FREQ_INPUT)) return n->freq;
    float lo = n->freq_min < PWM_FREQ_MIN ? PWM_FREQ_MIN : n->freq_min;
    float hi = n->freq_max < PWM_FREQ_MIN ? PWM_FREQ_MIN : n->freq_max;
    float f = pwm_fraction(in, n->in_full[1]);
    float freq = (n->options & PWM_FREQ_LOG) ? lo * powf(hi / lo, f) : lo + (hi - lo) * f;
    freq += 0.5f;
    return freq > 0xFFFF ? 0xFFFF : (uint16_t)freq;
}

// ── Pausing core 1 for flash writes ──
// A handshake through shared RAM, not the inter-core FIFO: the FIFO carries the
// protocol's words (PROTOCOL.md, Part 3), and the SDK's multicore lockout would
// consume them.
static volatile bool core1_running = false;
static volatile bool park_request = false;
static volatile bool parked = false;

// Runs from RAM with interrupts off, so nothing on core 1 touches flash until
// core 0 clears park_request.
static void __no_inline_not_in_flash_func(core1_park)(void) {
    uint32_t ints = save_and_disable_interrupts();
    parked = true;
    __dmb();
    while (park_request) {
        __wfe();
    }
    parked = false;
    __dmb();
    restore_interrupts(ints);
}

bool core1_pause(void) {
    if (!core1_running) return true;
    park_request = true;
    __dmb();
    absolute_time_t until = make_timeout_time_ms(1000);
    while (!parked) {
        if (time_reached(until)) {
            park_request = false;
            __dmb();
            __sev();
            return false;
        }
    }
    return true;
}

void core1_resume(void) {
    park_request = false;
    __dmb();
    __sev();
    while (parked) tight_loop_contents();
}

// Push a word to core 0. While core 0's FIFO is full, keep answering a park
// request: core 0 may be waiting for core 1 to park, and is not draining its FIFO
// meanwhile.
static void push_to_core0(uint32_t word) {
    while (!multicore_fifo_wready()) {
        if (park_request) core1_park();
    }
    multicore_fifo_push_blocking(word);
}

// ── Core 0's side of the FIFO ──
// Neither core may wait on the other while the other waits on it. Core 1 never
// does (see push_to_core0, which also answers park requests); core 0, while it
// waits (to send, or for core 1 to take a program), takes core 1's messages and
// keeps them, in order, for its main loop. A message is a tag and a value, sent
// one after the other, so once the tag is taken the value comes at once. Up to
// CORE1_HELD messages are kept; more are dropped.
#define CORE1_HELD 64

static uint32_t held[CORE1_HELD][2];
static uint8_t held_first = 0, held_count = 0;

static void hold_from_core1(void) {
    if (!multicore_fifo_rvalid()) return;
    uint32_t tag = multicore_fifo_pop_blocking();
    uint32_t value = multicore_fifo_pop_blocking();
    if (held_count == CORE1_HELD) {
        printf("core 1 message with tag %lu dropped: %d are waiting\n", (unsigned long)tag, CORE1_HELD);
        return;
    }
    uint8_t i = (held_first + held_count++) % CORE1_HELD;
    held[i][0] = tag;
    held[i][1] = value;
}

void push_to_core1(uint32_t word) {
    while (!multicore_fifo_wready()) hold_from_core1();
    multicore_fifo_push_blocking(word);
}

bool take_from_core1(uint32_t *tag, uint32_t *value) {
    if (held_count) {
        *tag = held[held_first][0];
        *value = held[held_first][1];
        held_first = (held_first + 1) % CORE1_HELD;
        held_count--;
        return true;
    }
    if (!multicore_fifo_rvalid()) return false;
    *tag = multicore_fifo_pop_blocking();
    *value = multicore_fifo_pop_blocking();
    return true;
}

// ── Programs ──
// Two buffers in RAM both cores see. Core 1 runs programs[running]; core 0
// compiles the next program into the other, then counts program_sent on. At its
// next scan core 1 takes it, switches running, and counts program_taken on: from
// then on the old buffer is core 0's to write.
static hw_program programs[2];
static volatile uint8_t running = 0;
static volatile uint32_t program_sent = 0, program_taken = 0;

hw_program *program_next(void) {
    absolute_time_t until = make_timeout_time_ms(1000);
    while (program_taken != program_sent) {
        if (time_reached(until)) return NULL;
        hold_from_core1();
    }
    __dmb();
    return &programs[running ^ 1];
}

void program_commit(void) {
    __dmb();
    program_sent = program_sent + 1;
    __dmb();
}

const hw_program *program_running(uint32_t *generation) {
    if (generation) *generation = program_taken;
    __dmb();
    return &programs[running];
}

// Core 1's state for each node of the program it runs.
typedef struct {
    bool ok;                    // set up: its pin was usable and free
    bool driven;                // driven since the program was taken
    bool on;                    // LED, GPO, GPIO input: its state; keyboard: whether its input is on
    uint8_t differ;             // GPIO input: reads in a row that differ from its state
    float duty;                 // PWM: the duty cycle it runs at, percent
    uint16_t freq;              // PWM: the frequency its slice runs at
    uint32_t top;               // PWM: counter steps in one period
    prog_logic_state logic;     // logic: what it keeps between scans
} node_state;

static node_state states[PROG_NODES_MAX];

// The pins core 1 has set up, one bit per GPIO. Taking a program resets only
// these: other pins belong to someone else, such as GPIO 23, 24, 25 and 29,
// which drive the WiFi chip on the Pico 2 W.
static uint32_t used_pins = 0;

static bool claim_pin(uint8_t pin) {
    if (!pin_usable(pin) || (used_pins & (1u << pin))) return false;
    used_pins |= 1u << pin;
    return true;
}

// Turn off every pin the last program set up, making them inputs again.
static void release_pins(void) {
    for (uint gpio = 0; gpio < 32; gpio++) {
        if (!(used_pins & (1u << gpio))) continue;
        if (gpio_get_function(gpio) == GPIO_FUNC_PWM) {
            pwm_set_enabled(pwm_gpio_to_slice_num(gpio), false);
            pwm_set_gpio_level(gpio, 0);
        }
        gpio_set_function(gpio, GPIO_FUNC_SIO);
        gpio_set_dir(gpio, GPIO_IN);
        gpio_disable_pulls(gpio);
    }
    used_pins = 0;
    memset(adc_inputs, 0, sizeof adc_inputs);
}

// Set up a node's hardware. Inputs come before outputs in a program, so an
// input wins a pin an output also names; otherwise the first node on a pin wins.
static bool setup_node(hw_program *p, const prog_node *n, node_state *st) {
    switch (n->type) {
    case MSG_HW_ADC: {
        uint8_t pin = ADC_PIN_FIRST + n->pin;
        if (!adc_inputs[n->pin].on) {
            if (!claim_pin(pin)) return false;
            adc_gpio_init(pin);
            adc_inputs[n->pin].on = true;
        }
        return true;
    }

    case MSG_HW_GPI:
        if (!claim_pin(n->pin)) return false;
        gpio_init(n->pin);
        gpio_set_dir(n->pin, GPIO_IN);
        switch (n->options & GPI_PULL_MASK) {
        case GPI_PULL_UP: gpio_pull_up(n->pin); break;
        case GPI_PULL_DOWN: gpio_pull_down(n->pin); break;
        default: gpio_disable_pulls(n->pin); break;
        }
        st->on = gpi_read(n->pin) != !!(n->options & GPI_INVERT);   // leaves the input buffer off
        p->signals[n->out].value = st->on;
        return true;

    case MSG_HW_CORE:
        if (core_temp.sampled) p->signals[n->out].value = core_temp.value;
        return true;

    case MSG_HW_GPO:
        if (!claim_pin(n->pin)) return false;
        gpio_init(n->pin);
        gpio_set_dir(n->pin, GPIO_OUT);
        return true;

    case MSG_HW_PWM:
        return claim_pin(n->pin);       // its slice starts at the first scan

    default:
        return true;
    }
}

static float signal_value(const hw_program *p, uint8_t s) {
    return s == PROG_NO_SIGNAL ? 0 : p->signals[s].value;
}


// Take the program core 0 sent: the signals of UI sources (buttons, sliders)
// keep their values from the last program, so a save doesn't move them, and
// each node's hardware is set up.
static void take_program(void) {
    __dmb();
    const hw_program *old = &programs[running];
    hw_program *p = &programs[running ^ 1];

    for (uint8_t i = 0; i < p->signal_count; i++) {
        prog_signal *s = &p->signals[i];
        if (!program_ui_source(s->type)) continue;
        uint8_t was = program_signal(old, s->id, s->output);
        if (was != PROG_NO_SIGNAL) s->value = old->signals[was].value;
    }

    release_pins();
    memset(states, 0, sizeof states);
    running ^= 1;
    __dmb();
    program_taken = program_sent;
    __dmb();

    for (uint8_t i = 0; i < p->node_count; i++) {
        const prog_node *n = &p->nodes[i];
        states[i].ok = setup_node(p, n, &states[i]);
        if (!states[i].ok) printf("hw man skipping type %02X on ID %u: GPIO %u is not usable or in use\n",
                                  n->type, n->id, n->type == MSG_HW_ADC ? ADC_PIN_FIRST + n->pin : n->pin);
        // A keyboard types when its input goes on, not because it was on when set up.
        if (n->type == MSG_HW_KEYBOARD) states[i].on = signal_value(p, n->in[0]) >= 0.5f;
        if (program_logic(n->type)) program_logic_start(p, n, &states[i].logic);
    }
    printf("hw man running a program of %u signals and %u nodes\n", p->signal_count, p->node_count);
}

// One scan: each node in the program's order, inputs then outputs. An output is
// driven when the value it would take changes, and at its first scan.
static void scan(uint32_t count) {
    hw_program *p = &programs[running];

    adc_sample_channels();
    if (count % TEMP_SAMPLE_SCANS == 0) core_temp_sample();

    for (uint8_t i = 0; i < p->node_count; i++) {
        const prog_node *n = &p->nodes[i];
        node_state *st = &states[i];
        if (!st->ok) continue;

        if (program_logic(n->type)) {
            p->signals[n->out].value = program_logic_step(p, n, p->signals[n->out].value, &st->logic, SCAN_MS);
            continue;
        }

        switch (n->type) {
        case MSG_HW_ADC:
            if (adc_inputs[n->pin].sampled)
                p->signals[n->out].value = adc_inputs[n->pin].raw * PROG_ADC_VOLTS / ADC_RAW_FULL;
            break;

        case MSG_HW_GPI: {
            bool on = gpi_read(n->pin) != !!(n->options & GPI_INVERT);
            if (on == st->on) {
                st->differ = 0;
            } else if (++st->differ >= GPI_DEBOUNCE) {
                st->on = on;
                st->differ = 0;
                p->signals[n->out].value = on;
            }
            break;
        }

        case MSG_HW_CORE:
            if (core_temp.sampled) p->signals[n->out].value = core_temp.value;
            break;


        case MSG_HW_LED: {
            bool on = signal_value(p, n->in[0]) >= 0.5f;
            if (st->driven && on == st->on) break;
            // The LED is on the WiFi chip, which only core 0 talks to.
            push_to_core0(FIFO_TAG_LED);
            push_to_core0(on);
            st->on = on;
            st->driven = true;
            break;
        }

        case MSG_HW_GPO: {
            bool on = signal_value(p, n->in[0]) >= 0.5f;
            if (st->driven && on == st->on) break;
            gpio_put(n->pin, on);
            st->on = on;
            st->driven = true;
            break;
        }

        case MSG_HW_KEYBOARD: {
            // Core 0 types the keystroke, once each time the input goes on.
            bool on = signal_value(p, n->in[0]) >= 0.5f;
            if (on && !st->on) {
                push_to_core0(FIFO_TAG_KEYBOARD);
                push_to_core0(n->id);
            }
            st->on = on;
            break;
        }

        case MSG_HW_PWM: {
            float duty = pwm_duty(n, signal_value(p, n->in[0]));
            uint16_t freq = pwm_freq(n, signal_value(p, n->in[1]));
            if (st->driven && duty == st->duty && freq == st->freq) break;
            if (!st->driven || freq != st->freq) {
                if (!st->driven) printf("PWM on GPIO %u at %u Hz\n", n->pin, freq);
                st->top = pwm_set_freq(n->pin, freq);
                st->freq = freq;
            }
            pwm_set_gpio_level(n->pin, (uint16_t)(st->top * duty / PWM_DUTY_MAX + 0.5f));
            st->duty = duty;
            st->driven = true;
            break;
        }

        default:
            break;
        }
    }
}

// A command from the browser: the new value of an output of a UI source, 0 to
// VALUE_FULL, which becomes its signal's value, 0 to 1.
static void command(uint32_t word) {
    hw_program *p = &programs[running];
    uint8_t s = program_signal(p, fifo_op_id(word), fifo_op_output(word));
    if (s == PROG_NO_SIGNAL || !program_ui_source(p->signals[s].type)) {
        printf("hw man: no source n%u output %u\n", fifo_op_id(word), fifo_op_output(word));
        return;
    }
    p->signals[s].value = (float)fifo_op_value(word) / VALUE_FULL;
}

void core1_worker() {
    printf("(core1) started\n");

    adc_init();
    adc_set_temp_sensor_enabled(true);

    absolute_time_t start = get_absolute_time();
    absolute_time_t next_scan = start;
    uint32_t scans = 0;
    uint64_t next_uptime_timeout = 5;

    core1_running = true;
    __dmb();

    while(1) {
        // A safe point: core 1 holds no locks here.
        if (park_request) core1_park();
        if (program_taken != program_sent) take_program();

        if (time_reached(next_scan)) {
            scan(scans++);
            next_scan = delayed_by_ms(next_scan, SCAN_MS);
            // Behind, as after a pause for a flash write: start again from now.
            if (time_reached(next_scan)) next_scan = make_timeout_time_ms(SCAN_MS);
        }

        int64_t up_time_elapsed = absolute_time_diff_us(start, get_absolute_time())/1200000;
        if (up_time_elapsed >= next_uptime_timeout) {
            push_to_core0(FIFO_TAG_UPTIME);
            push_to_core0((uint32_t)up_time_elapsed);
            next_uptime_timeout = up_time_elapsed + 5;
        }

        // Until the next scan, take core 0's words: each is an action word.
        int64_t wait = absolute_time_diff_us(get_absolute_time(), next_scan);
        uint32_t op;
        if (multicore_fifo_pop_timeout_us(wait > 0 ? (uint64_t)wait : 0, &op)) {
            if (fifo_op(op) == FIFO_OP_CONTROL_ACTION) command(op);
        }
    }
}
