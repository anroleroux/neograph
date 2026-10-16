#include <math.h>
#include <stdio.h>
#include <string.h>
#include "uthash.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/cyw43_arch.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

#include "hwman.h"
#include "config.h"
#include "uthash.h"

#define ADC_TEMP_SENSOR 4        // Internal temperature sensor is connected to ADC 4
#define TEMP_AVERAGE    16       // reads averaged into one sample

// The chip's temperature in °C: TEMP_AVERAGE reads of the sensor, averaged. The
// sensor reads 0.706 V at 27 °C and falls 1.721 mV per °C (RP2350 datasheet).
static float read_internal_temperature(void) {
    adc_select_input(ADC_TEMP_SENSOR);
    sleep_us(10);  // Allow ADC to stabilize
    uint32_t sum = 0;
    for (int i = 0; i < TEMP_AVERAGE; i++) sum += adc_read();

    float voltage = (sum * 3.3f) / (TEMP_AVERAGE * 4095.0f);
    return 27.0f - ((voltage - 0.706f) / 0.001721f);
}

typedef struct {
    uint8_t operation;
    uint8_t control_type;
    uint16_t id;
    union {
        uint8_t io;
        uint8_t value;
    };
    uint32_t source;    // the output driving it, as source_key
    uint8_t drives;     // PWM: the parameter this source drives, PWM_DRIVES_DUTY or _FREQ
    bool on;            // keyboard: whether its input is on, to type once each time it goes on
} hwel;

typedef struct {
    uint32_t key;       // source_key: the source's element ID and output
    uint8_t len;
    uint8_t size;
    hwel *hwels;

    UT_hash_handle hh;
} hwel_sources;

hwel decode_hwel(uint32_t op) {
    hwel result;

    result.operation    = fifo_op(op);
    result.control_type = fifo_op_type(op);
    result.id           = fifo_op_id(op);
    result.value        = fifo_op_value(op);
    result.drives       = 0;
    result.on           = false;

    return result;
}

// Outputs are kept by the output of a source that drives them: its element ID,
// and which of its outputs (a 2D slider has two).
static uint32_t source_key(uint16_t id, uint8_t output) { return (uint32_t)id << 8 | output; }

void decode_hwrf(uint32_t rf,hwel *decop) {
    decop->source = source_key(fifo_ref_source(rf), fifo_ref_output(rf));
}

static hwel_sources *hwel_source_table = NULL;

// ── PWM outputs ──
// A PWM's duty cycle and frequency are each constant or follow an input (its
// PWM_DUTY_INPUT and PWM_FREQ_INPUT options), so a PWM is in the source table
// up to twice: under its duty source and under its frequency source, each
// entry with the parameter it drives. The state they share is kept by pin.
#define PWM_DRIVES_DUTY 0
#define PWM_DRIVES_FREQ 1
#define PWM_PINS 30

typedef struct {
    uint8_t options;
    uint8_t duty, duty_min, duty_max;       // percent
    uint16_t freq, freq_min, freq_max;      // Hz
    uint8_t duty_in, freq_in;               // the inputs' last values, 0 to VALUE_FULL
    uint16_t freq_now;                      // the frequency the slice runs at
    uint32_t top;                           // counter steps in one period
} pwm_out;

static pwm_out pwm_outs[PWM_PINS];

// The pins core 1 has set up as a GPO or PWM, one bit per GPIO. A clean setup
// resets only these: other pins belong to someone else, such as GPIO 23, 24,
// 25 and 29, which drive the WiFi chip on the Pico 2 W.
static uint32_t used_pins = 0;

// From a at value 0 to b at VALUE_FULL, linearly. b may be below a.
static uint32_t pwm_lerp(uint32_t a, uint32_t b, uint8_t value) {
    return (uint32_t)((int32_t)a + ((int32_t)b - (int32_t)a) * value / VALUE_FULL);
}

// From a at value 0 to b at VALUE_FULL, logarithmically: each step multiplies by
// the same factor, so a sweep spends as long on 100-1000 Hz as on 1-10 kHz. a and
// b are at least PWM_FREQ_MIN, so neither is 0; b may be below a.
static uint32_t pwm_lerp_log(uint32_t a, uint32_t b, uint8_t value) {
    float f = a * powf((float)b / a, (float)value / VALUE_FULL) + 0.5f;
    return f > 0xFFFF ? 0xFFFF : (uint32_t)f;
}

// Run a pin's PWM slice at freq Hz and return the counter steps in one period.
// The clock divider is the smallest integer that lets one period fit the 16-bit
// counter: an integer divider keeps the period free of jitter, and the smallest
// one gives the finest duty steps. The slice is shared with one other pin,
// which gets the same frequency.
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

// Drive a PWM pin from its state: the frequency and duty cycle, each constant
// or from its input's last value. The slice is reprogrammed only when the
// frequency changes.
static void pwm_apply(uint pin) {
    pwm_out *p = &pwm_outs[pin];

    uint16_t freq = p->freq;
    if (p->options & PWM_FREQ_INPUT) {
        uint16_t lo = p->freq_min < PWM_FREQ_MIN ? PWM_FREQ_MIN : p->freq_min;
        uint16_t hi = p->freq_max < PWM_FREQ_MIN ? PWM_FREQ_MIN : p->freq_max;
        freq = (p->options & PWM_FREQ_LOG) ? pwm_lerp_log(lo, hi, p->freq_in) : pwm_lerp(lo, hi, p->freq_in);
    }
    if (freq != p->freq_now) {
        p->top = pwm_set_freq(pin, freq);
        p->freq_now = freq;
    }

    // The duty in percent × VALUE_FULL, to keep the input's resolution.
    uint32_t duty = (p->options & PWM_DUTY_INPUT)
        ? pwm_lerp(p->duty_min * VALUE_FULL, p->duty_max * VALUE_FULL, p->duty_in)
        : p->duty * VALUE_FULL;
    if (duty > PWM_DUTY_MAX * VALUE_FULL) duty = PWM_DUTY_MAX * VALUE_FULL;
    pwm_set_gpio_level(pin, p->top * duty / (PWM_DUTY_MAX * VALUE_FULL));
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
// Neither core may wait for room in the other's FIFO while the other waits for
// room in its own: neither would ever drain the other's. Core 1 never does (see
// push_to_core0, which also answers park requests); core 0, while it waits to
// send, takes core 1's messages and keeps them, in order, for its main loop. A
// message is a tag and a value, sent one after the other, so once the tag is
// taken the value comes at once. Up to CORE1_HELD messages are kept; more, which
// would take core 1 sending for a long while as core 0 waits, are dropped.
#define CORE1_HELD 64

static uint32_t held[CORE1_HELD][2];
static uint8_t held_first = 0, held_count = 0;

void push_to_core1(uint32_t word) {
    while (!multicore_fifo_wready()) {
        if (!multicore_fifo_rvalid()) continue;
        uint32_t tag = multicore_fifo_pop_blocking();
        uint32_t value = multicore_fifo_pop_blocking();
        if (held_count == CORE1_HELD) {
            printf("core 1 message with tag %lu dropped: %d are waiting\n", (unsigned long)tag, CORE1_HELD);
            continue;
        }
        uint8_t i = (held_first + held_count++) % CORE1_HELD;
        held[i][0] = tag;
        held[i][1] = value;
    }
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

void add_packet(hwel el) {
    // First check if one with the same ID already exists
    hwel_sources *existing = NULL;
    HASH_FIND(hh, hwel_source_table, &el.source, sizeof(uint32_t), existing);
    if (existing) {
        if (existing->len == existing->size) {
            existing->size += 5;
            hwel *temp = realloc(existing->hwels, existing->size * sizeof(hwel_sources));
            if (temp == NULL) {
                printf("hwel reallocation failed\n");
                free(existing->hwels);
                return;
            }
            existing->hwels = temp;

        }
        printf("adding another hwel allocation to src:%lX, type:%02X, id:%d\n",(unsigned long)existing->key, el.control_type,el.id);
        existing->len++;
        existing->hwels[existing->len-1] = el;

        return;
    }

    printf("new hwel_sources allocation, src:%lX\n",(unsigned long)el.source);
    hwel_sources *new_srcsel = malloc(sizeof(hwel_sources));
    if (new_srcsel == NULL) {
        printf("hwel_sources allocation failed\n");
        return;
    }

    new_srcsel->size = 5;
    new_srcsel->len = 1;
    printf("adding hwel allocation, type:%02X, id:%d\n",el.control_type,el.id);
    new_srcsel->hwels = malloc(new_srcsel->size * sizeof(hwel));
    if (new_srcsel->hwels == NULL) {
        printf("hwel allocation failed\n");
        return;
    }
    new_srcsel->hwels[0] = el;
    new_srcsel->key = el.source;

    HASH_ADD(hh, hwel_source_table, key, sizeof(uint32_t), new_srcsel);
}

hwel_sources *find_packet(uint32_t key) {
    hwel_sources *srcsel = NULL;
    HASH_FIND(hh, hwel_source_table, &key, sizeof(uint32_t), srcsel);
    return srcsel;
}

void delete_all_packets() {
    hwel_sources *current, *tmp;
    HASH_ITER(hh, hwel_source_table, current, tmp) {
        free(current->hwels);
        HASH_DEL(hwel_source_table, current);
        free(current);
    }
}

// Drive the outputs wired to an output of a source with its new value, 0 to
// VALUE_FULL. Returns false if nothing is wired to it. `log` prints each output
// driven: a command from the browser logs, an ADC reading, which changes often, doesn't.
static bool drive_outputs(uint16_t source_id, uint8_t output, uint8_t value, bool log) {
    hwel_sources *hw_srcs = find_packet(source_key(source_id, output));
    if (!hw_srcs) return false;

    bool on = value >= VALUE_HALF;     // for the on/off outputs
    for (size_t i = 0; i < hw_srcs->len; i++) {
        hwel el = hw_srcs->hwels[i];

        switch (el.control_type) {
        case MSG_HW_LED:
            if (log) printf("switch LED, value:%i\n", value);
            push_to_core0(FIFO_TAG_LED);
            push_to_core0(on);
            break;

        case MSG_HW_GPO:
            if (log) printf("switch GPO %d, value:%i\n", el.io, value);
            gpio_put(el.io, on);
            break;

        case MSG_HW_KEYBOARD:
            // Core 0 types the keystroke, once each time the input goes on.
            if (on && !el.on) {
                if (log) printf("keyboard %d pressed\n", el.id);
                push_to_core0(FIFO_TAG_KEYBOARD);
                push_to_core0(el.id);
            }
            hw_srcs->hwels[i].on = on;
            break;

        case MSG_HW_PWM:
            if (log) printf("switch PWM %d, value:%i\n", el.io, value);
            if (el.io >= PWM_PINS) break;
            if (el.drives == PWM_DRIVES_FREQ) pwm_outs[el.io].freq_in = value;
            else pwm_outs[el.io].duty_in = value;
            pwm_apply(el.io);
            break;

        default:
            break;
        }
    }
    return true;
}

// ── ADC inputs ──
// Each channel with an ADC node is sampled every ADC_SAMPLE_MS. When its value
// (0 to VALUE_FULL, as a command's) changes, it drives the outputs wired to its
// nodes, as a command from the browser would. Its reading is reported to core 0
// for the browser every ADC_REPORT_MS, when it has moved by ADC_REPORT_DELTA or
// ADC_REPORT_MAX_MS has passed since the last report.
#define ADC_SAMPLE_MS       10
#define ADC_AVERAGE         16      // reads averaged into one sample
#define ADC_HYSTERESIS      12      // counts from the middle of the value's step before it changes; a step is ~16
#define ADC_REPORT_MS       100
#define ADC_REPORT_DELTA    8
#define ADC_REPORT_MAX_MS   1000
#define ADC_NODES_MAX       16

typedef struct {
    bool on;
    bool drive;                 // drive the outputs at the next sample, whatever its value
    bool sampled;
    uint16_t raw;               // the last sample, 0 to ADC_RAW_FULL
    uint8_t value;              // the value the outputs were last driven with
    uint16_t reported;          // the last reading reported; above ADC_RAW_FULL if none
    absolute_time_t reported_at;
} adc_input;

typedef struct {
    uint16_t id;
    uint8_t channel;
} adc_node;

static adc_input adc_inputs[ADC_CHANNELS];
static adc_node adc_nodes[ADC_NODES_MAX];
static uint8_t adc_node_count = 0;

static uint16_t adc_sample(uint8_t channel) {
    adc_select_input(channel);
    uint32_t sum = 0;
    for (int i = 0; i < ADC_AVERAGE; i++) sum += adc_read();
    return (sum + ADC_AVERAGE / 2) / ADC_AVERAGE;
}

// Sample each channel, and drive the outputs of its nodes with a new value. The
// value changes only once the reading is ADC_HYSTERESIS counts from the middle
// of the value's step, so noise at the edge of a step doesn't flicker the outputs.
static void adc_poll(void) {
    for (uint8_t ch = 0; ch < ADC_CHANNELS; ch++) {
        adc_input *in = &adc_inputs[ch];
        if (!in->on) continue;

        in->raw = adc_sample(ch);
        in->sampled = true;
        uint8_t value = (in->raw * VALUE_FULL + ADC_RAW_FULL / 2) / ADC_RAW_FULL;
        int32_t middle = (in->value * ADC_RAW_FULL + VALUE_FULL / 2) / VALUE_FULL;
        int32_t off = (int32_t)in->raw - middle;
        if (!in->drive && (value == in->value || (off < ADC_HYSTERESIS && -off < ADC_HYSTERESIS))) continue;

        in->drive = false;
        in->value = value;
        for (uint8_t i = 0; i < adc_node_count; i++) {
            if (adc_nodes[i].channel == ch) drive_outputs(adc_nodes[i].id, 0, value, false);
        }
    }
}

static void adc_report(absolute_time_t now) {
    for (uint8_t ch = 0; ch < ADC_CHANNELS; ch++) {
        adc_input *in = &adc_inputs[ch];
        if (!in->on || !in->sampled) continue;

        int32_t moved = (int32_t)in->raw - in->reported;
        if (moved < ADC_REPORT_DELTA && -moved < ADC_REPORT_DELTA
            && absolute_time_diff_us(in->reported_at, now) < ADC_REPORT_MAX_MS * 1000) continue;

        push_to_core0(FIFO_TAG_ADC);
        push_to_core0(fifo_adc_word(ch, in->raw));
        in->reported = in->raw;
        in->reported_at = now;
    }
}

// Set up an ADC node on its channel. Returns false if it can't be: the channel is
// out of range, its pin is used by a GPO or PWM, or there are ADC_NODES_MAX nodes.
// Several nodes may share a channel.
static bool adc_setup(uint16_t id, uint8_t channel) {
    if (channel >= ADC_CHANNELS || adc_node_count == ADC_NODES_MAX) return false;
    adc_input *in = &adc_inputs[channel];
    uint pin = ADC_PIN_FIRST + channel;

    if (!in->on) {
        if (used_pins & (1u << pin)) return false;
        adc_gpio_init(pin);
        used_pins |= 1u << pin;
        *in = (adc_input){ .on = true, .reported = 0xFFFF };
    }
    adc_nodes[adc_node_count++] = (adc_node){ id, channel };
    printf("ADC%d on GPIO %d for ID %d\n", channel, pin, id);
    return true;
}

// ── GPIO inputs ──
// Each GPIO input node's pin is read every GPI_SAMPLE_MS. A new state, held for
// GPI_DEBOUNCE reads in a row so a bouncing switch changes it once, drives the
// outputs wired to the node (VALUE_FULL on, 0 off), as a command from the browser
// would, and is reported to core 0 for the browser. The state is also reported
// every GPI_REPORT_MS, so a browser that connects later learns it.
#define GPI_SAMPLE_MS       10
#define GPI_DEBOUNCE        3
#define GPI_REPORT_MS       1000
#define GPI_NODES_MAX       16

typedef struct {
    uint16_t id;
    uint8_t pin;
    bool invert;                // on while the pin is low
    bool on;                    // the debounced state
    uint8_t differ;             // reads in a row that differ from it
    bool drive;                 // drive the outputs at the next read, whatever the state
    absolute_time_t reported_at;
} gpi_node;

static gpi_node gpi_nodes[GPI_NODES_MAX];
static uint8_t gpi_node_count = 0;

static void gpi_report(gpi_node *in, absolute_time_t now) {
    push_to_core0(FIFO_TAG_GPI);
    push_to_core0(fifo_gpi_word(in->pin, in->on));
    in->reported_at = now;
}

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

static void gpi_poll(absolute_time_t now) {
    for (uint8_t i = 0; i < gpi_node_count; i++) {
        gpi_node *in = &gpi_nodes[i];
        bool on = gpi_read(in->pin) != in->invert;
        if (on == in->on) {
            in->differ = 0;
        } else if (++in->differ >= GPI_DEBOUNCE) {
            in->on = on;
            in->differ = 0;
            in->drive = true;
            gpi_report(in, now);
        }
        if (in->drive) {
            in->drive = false;
            drive_outputs(in->id, 0, in->on ? VALUE_FULL : 0, false);
        }
        if (absolute_time_diff_us(in->reported_at, now) >= GPI_REPORT_MS * 1000) gpi_report(in, now);
    }
}

// Set up a GPIO input node on its pin, with its pull. Returns false if it can't
// be: the pin isn't usable or is in use, or there are GPI_NODES_MAX nodes.
static bool gpi_setup(uint16_t id, uint8_t pin, uint8_t options) {
    if (!pin_usable(pin) || (used_pins & (1u << pin)) || gpi_node_count == GPI_NODES_MAX) return false;
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    switch (options & GPI_PULL_MASK) {
    case GPI_PULL_UP: gpio_pull_up(pin); break;
    case GPI_PULL_DOWN: gpio_pull_down(pin); break;
    default: gpio_disable_pulls(pin); break;
    }
    used_pins |= 1u << pin;

    // Reported and driven at the first read.
    gpi_node *in = &gpi_nodes[gpi_node_count++];
    *in = (gpi_node){ .id = id, .pin = pin, .invert = options & GPI_INVERT, .drive = true,
                      .reported_at = from_us_since_boot(0) };
    in->on = gpi_read(pin) != in->invert;      // leaves the input buffer off (see gpi_read)
    printf("GPI on GPIO %d for ID %d, options %02X\n", pin, id, options);
    return true;
}

// ── Core temperature ──
// The chip's temperature is sampled every TEMP_SAMPLE_MS and smoothed, whatever
// the graph: each sample moves it 1/TEMP_FILTER of the way, so it follows over
// about a second and a sample's noise (a sensor count is ~0.5 °C) mostly cancels.
// It is reported to core 0 for the browser every TEMP_REPORT_MS. Each core
// temperature node is a source: the outputs wired to it are driven with the
// temperature from 0 °C (0) to CORE_TEMP_FULL °C (VALUE_FULL), when that value
// changes, with hysteresis as an ADC's.
#define TEMP_SAMPLE_MS      100
#define TEMP_FILTER         8
#define TEMP_HYSTERESIS     0.75f   // of a value step from the middle of the current one
#define TEMP_REPORT_MS      1000
#define CORE_NODES_MAX      16

static struct {
    bool sampled;
    float celsius;              // smoothed
    uint8_t value;              // the value the outputs were last driven with
    bool drive;                 // drive the outputs at the next sample, whatever the value
} core_temp;

static uint16_t core_nodes[CORE_NODES_MAX];
static uint8_t core_node_count = 0;

// The temperature as a value, 0 °C at 0 to CORE_TEMP_FULL °C at VALUE_FULL.
static uint8_t core_temp_value(float celsius) {
    float v = celsius * VALUE_FULL / CORE_TEMP_FULL + 0.5f;
    return v <= 0 ? 0 : v >= VALUE_FULL ? VALUE_FULL : (uint8_t)v;
}

static void core_temp_poll(void) {
    float sample = read_internal_temperature();
    if (!core_temp.sampled) core_temp.celsius = sample;
    else core_temp.celsius += (sample - core_temp.celsius) / TEMP_FILTER;
    core_temp.sampled = true;

    uint8_t value = core_temp_value(core_temp.celsius);
    float step = (float)CORE_TEMP_FULL / VALUE_FULL;
    float off = core_temp.celsius - core_temp.value * step;
    if (!core_temp.drive && (value == core_temp.value || (off < TEMP_HYSTERESIS * step && -off < TEMP_HYSTERESIS * step))) return;

    core_temp.drive = false;
    core_temp.value = value;
    for (uint8_t i = 0; i < core_node_count; i++) drive_outputs(core_nodes[i], 0, value, false);
}

static void core_temp_report(void) {
    if (!core_temp.sampled) return;
    push_to_core0(FIFO_TAG_TEMPERATURE);
    uint32_t data;
    memcpy(&data, &core_temp.celsius, sizeof(float));
    push_to_core0(data);
}

// Set up a core temperature node. Returns false if there are CORE_NODES_MAX.
static bool core_temp_setup(uint16_t id) {
    if (core_node_count == CORE_NODES_MAX) return false;
    core_nodes[core_node_count++] = id;
    core_temp.drive = true;
    printf("core temperature for ID %d\n", id);
    return true;
}

void core1_worker() {
    printf("(core1) started\n");
    
    adc_init();
    adc_set_temp_sensor_enabled(true);

    absolute_time_t start = get_absolute_time();
    uint64_t next_uptime_timeout = 5;
    absolute_time_t next_temp_sample = start;
    absolute_time_t next_temp_report = delayed_by_ms(start, TEMP_REPORT_MS);
    absolute_time_t next_adc_sample = start;
    absolute_time_t next_adc_report = start;
    absolute_time_t next_gpi_sample = start;

    core1_running = true;
    __dmb();

    while(1) {
        // A safe point: core 1 holds no locks here.
        if (park_request) core1_park();

        int64_t up_time_elapsed = absolute_time_diff_us(start, get_absolute_time())/1200000;
        if (up_time_elapsed >= next_uptime_timeout) {
            push_to_core0(FIFO_TAG_UPTIME);
            push_to_core0((uint32_t)up_time_elapsed);
            next_uptime_timeout = up_time_elapsed + 5;
        }

        absolute_time_t now = get_absolute_time();
        if (time_reached(next_adc_sample)) {
            adc_poll();
            next_adc_sample = delayed_by_ms(now, ADC_SAMPLE_MS);
        }
        if (time_reached(next_adc_report)) {
            adc_report(now);
            next_adc_report = delayed_by_ms(now, ADC_REPORT_MS);
        }
        if (time_reached(next_gpi_sample)) {
            gpi_poll(now);
            next_gpi_sample = delayed_by_ms(now, GPI_SAMPLE_MS);
        }
        if (time_reached(next_temp_sample)) {
            core_temp_poll();
            next_temp_sample = delayed_by_ms(now, TEMP_SAMPLE_MS);
        }
        if (time_reached(next_temp_report)) {
            core_temp_report();
            next_temp_report = delayed_by_ms(now, TEMP_REPORT_MS);
        }

        uint32_t op;
        if (multicore_fifo_pop_timeout_us(1000, &op)) {
            hwel decop = decode_hwel(op);
            // printf("hw opp %d type %d, io %d\n",decop.operation, decop.control_type, decop.io);
            
            switch (decop.operation) {
            case FIFO_OP_SETUP: {
                // An output on a pin it may not use (see PIN_USABLE_MASK), or on
                // one already in use, is not set up: the first setup on a pin wins.
                bool usable = true;
                switch (decop.control_type) {
                case MSG_HW_LED:
                    multicore_fifo_pop_timeout_us(1000, &op);
                    decode_hwrf(op,&decop);
                    break;

                case MSG_HW_KEYBOARD:
                    multicore_fifo_pop_timeout_us(1000, &op);
                    decode_hwrf(op,&decop);
                    break;

                case MSG_HW_GPO:
                    multicore_fifo_pop_timeout_us(1000, &op);
                    decode_hwrf(op,&decop);

                    usable = pin_usable(decop.io) && !(used_pins & (1u << decop.io));
                    if (!usable) break;
                    gpio_init(decop.io);
                    gpio_set_dir(decop.io, GPIO_OUT);
                    used_pins |= 1u << decop.io;
                    break;
                
                case MSG_HW_PWM: {
                    // Reference (duty source), PWM, duty, reference (frequency
                    // source) and frequency words follow.
                    uint32_t w[5] = {0};
                    for (int i = 0; i < 5; i++) multicore_fifo_pop_timeout_us(1000, &w[i]);
                    decode_hwrf(w[0], &decop);

                    uint pin = decop.io;
                    usable = pin_usable(pin) && !(used_pins & (1u << pin));
                    if (!usable) break;
                    used_pins |= 1u << pin;
                    pwm_out *p = &pwm_outs[pin];
                    *p = (pwm_out){
                        .options  = fifo_pwm_options(w[1]),
                        .freq     = fifo_pwm_freq(w[1]),
                        .duty     = fifo_duty_const(w[2]),
                        .duty_min = fifo_duty_min(w[2]),
                        .duty_max = fifo_duty_max(w[2]),
                        .freq_min = fifo_freq_min(w[4]),
                        .freq_max = fifo_freq_max(w[4]),
                    };
                    // Until its inputs send values they read 0: each parameter
                    // from an input starts at its min.
                    pwm_apply(pin);
                    printf("PWM on GPIO %d: options %02X, %d Hz\n", pin, p->options, p->freq_now);

                    // Filed under the duty source below, as every output; and
                    // under the frequency source here.
                    decop.drives = PWM_DRIVES_DUTY;
                    hwel freq_el = decop;
                    freq_el.drives = PWM_DRIVES_FREQ;
                    decode_hwrf(w[3], &freq_el);
                    if (fifo_ref_source(w[3])) add_packet(freq_el);
                    break;
                }

                case MSG_HW_ADC:
                    // A source, not an output: nothing goes in the table under it
                    // here, but its outputs do, under its ID.
                    if (!adc_setup(decop.id, decop.io)) {
                        printf("hw man skipping ADC on ID %i: channel %d is not usable or its pin is in use\n",
                               decop.id, decop.io);
                    }
                    break;

                case MSG_HW_GPI:
                    // A source, as an ADC: its options word follows.
                    multicore_fifo_pop_timeout_us(1000, &op);
                    if (!gpi_setup(decop.id, decop.io, fifo_gpi_options(op))) {
                        printf("hw man skipping GPI on ID %i: GPIO %d is not usable or in use\n",
                               decop.id, decop.io);
                    }
                    break;

                case MSG_HW_CORE:
                    // A source, as an ADC, on the chip's own sensor.
                    if (!core_temp_setup(decop.id)) {
                        printf("hw man skipping core temperature on ID %i: too many\n", decop.id);
                    }
                    break;

                default:
                    break;
                }

                if (decop.control_type == MSG_HW_ADC || decop.control_type == MSG_HW_GPI
                    || decop.control_type == MSG_HW_CORE) break;
                if (!usable) {
                    printf("hw man skipping control type %02X on ID %i: GPIO %d is not usable or in use\n",
                           decop.control_type, decop.id, decop.io);
                    break;
                }
                add_packet(decop);
                printf("hw man adding control type %02X on ID %i\n",decop.control_type,decop.id);

                // An output set up after its ADC, GPI or core temperature source
                // was read gets the source's value at the next read, not at its
                // next change.
                for (uint8_t ch = 0; ch < ADC_CHANNELS; ch++) adc_inputs[ch].drive = true;
                for (uint8_t i = 0; i < gpi_node_count; i++) gpi_nodes[i].drive = true;
                core_temp.drive = true;
                break;
            }

            case FIFO_OP_CLEAN_SETUP:
                delete_all_packets();
                memset(pwm_outs, 0, sizeof pwm_outs);
                memset(adc_inputs, 0, sizeof adc_inputs);
                adc_node_count = 0;
                gpi_node_count = 0;
                core_node_count = 0;
                for (uint gpio = 0; gpio < PWM_PINS; gpio++) {
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
                printf("hw man cleared controls\n");
                break;

            case FIFO_OP_CONTROL_ACTION:
                printf("hw man buffer %02X %02X %02X %02X\n",op>>24&0xFF,op>>16&0xFF,op>>8&0xFF,op&0xFF);
                if (!drive_outputs(decop.id, fifo_op_output(op), decop.value, true)) {
                    printf("hw sources with ID %d, output %d not found\n",decop.id,fifo_op_output(op));
                }
                break;
            }
        }
    }
}
