#include <math.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/irq.h"

#include "clock.h"
#include "ds18b20.h"
#include "encoder.h"
#include "hwman.h"
#include "neopixel.h"
#include "program.h"
#include "protocol.h"
#include "servo.h"
#include "stepper.h"
#include "ultrasonic.h"
#include "ws2812.pio.h"

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

// ── The clock ──
// One time of day for the device (MSG_HW_CLOCK), in its time zone: the milliseconds the
// timer has counted since boot, plus clock_offset_ms, a day round. It starts at 00:00:00
// at boot; core 0 sets it (from the browser, or an internet time server: timesync.c),
// and core 1 reads it for the clock nodes' signals each scan. The offset is one 32-bit
// word, so core 1 never reads half of a new one.
static volatile uint32_t clock_offset_ms = 0;
static bool clock_was_set = false;
static int32_t clock_zone = 0;          // minutes ahead of UTC

bool clock_set(uint8_t h, uint8_t m, uint8_t s) {
    if (h >= CLOCK_HOURS || m >= CLOCK_MINUTES || s >= CLOCK_SECONDS) return false;
    clock_set_ms(((uint32_t)h * 3600 + (uint32_t)m * 60 + s) * 1000);
    return true;
}

void clock_set_ms(uint32_t ms) {
    clock_offset_ms = clock_offset_to(time_us_64() / 1000, ms);
    clock_was_set = true;
    __dmb();
}

void clock_set_zone(int32_t zone) {
    if (zone == clock_zone) return;
    if (clock_was_set) clock_offset_ms = clock_shift(clock_offset_ms, zone - clock_zone);
    clock_zone = zone;
    __dmb();
}

static uint32_t clock_now(void) {
    return clock_time_of_day(time_us_64() / 1000, clock_offset_ms);
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
static void ultrasonics_drop(void);

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
    ultrasonics_drop();     // an echo timed across the pause is wrong
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
    bool on;                    // LED, GPO, GPIO input: its state; keyboard: whether its input is on;
                                // NeoPixels: whether their on/off input lets them show
    uint8_t differ;             // GPIO input: reads in a row that differ from its state
    float duty;                 // PWM: the duty cycle it runs at, percent
    uint16_t freq;              // PWM: the frequency its slice runs at
    uint32_t top;               // PWM: counter steps in one period
    prog_logic_state logic;     // logic: what it keeps between scans
    float angle;                // servo: the angle it is at, degrees
    float in;                   // stepper: its input at the last scan
    stepper_motion motion;      // stepper: its steps a revolution, top speed and acceleration
    stepper_state stepper;      // stepper: where it is, and how fast it turns
    uint64_t stepped_us;        // stepper: when stepper_advance last ran
    float fraction;             // NeoPixels: the input they show, as a fraction of its full scale
    uint32_t *leds;             // NeoPixels: each LED's colour, in neopixel_pool, which DMA sends out
    PIO pio;                    // NeoPixels: the state machine that sends the bits, and its program
    uint sm, offset;
    int dma;                    // NeoPixels: the DMA channel that feeds it
    uint64_t ready_us;          // NeoPixels: when the last send has gone out and latched
    struct {
        uint8_t step;           // what its next scan does (OW_)
        int32_t wait_ms;        // until its conversion is done, or it tries again
        uint8_t got;            // scratchpad bytes read
        uint8_t pad[DS18B20_PAD_LEN];
        uint8_t fails;          // reads in a row that failed
        bool found;             // it answers: its output 1
        bool configured;        // its resolution is written
        bool doubted;           // an 85 °C reading was dropped (ds18b20_take)
    } ds;                       // DS18B20
    uint8_t encoder;            // rotary encoder: its place in encoders[]; `on` and `differ`
                                // debounce its switch, as a GPIO input's
    uint8_t ultrasonic;         // ultrasonic ranger: its place in ultrasonics[]; `on`: a ping is
                                // out; `differ`: its pings in a row without an echo
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

// ── NeoPixel outputs ──
// Each NeoPixel node takes a PIO state machine running ws2812.pio on its pin, and a DMA
// channel that feeds it the LEDs' colours from neopixel_pool, so core 1 doesn't wait while
// they go out: 30 µs an LED at 800 kHz, so 7.7 ms for NEOPIXEL_LEDS_MAX. It sends them
// again when what they show changes, once the last send has gone out and the line has
// been low NEOPIXEL_RESET_US, which latches it.
#define NEOPIXEL_BIT_HZ     800000
#define NEOPIXEL_LED_US     30          // 24 bits of 1.25 µs
#define NEOPIXEL_RESET_US   300         // low this long, a WS2812B shows what it took (older ones 50 µs)
#define NEOPIXEL_LEDS_TOTAL 1024        // LEDs on all the program's NeoPixel nodes

static uint32_t neopixel_pool[NEOPIXEL_LEDS_TOTAL];
static uint16_t neopixel_pool_used = 0;

static bool neopixel_setup(const prog_node *n, node_state *st) {
    if (n->px_feeds) return true;       // the last node of its chain drives the LEDs
    if (neopixel_pool_used + n->px.leds > NEOPIXEL_LEDS_TOTAL) {
        printf("NeoPixels n%u: more than %u LEDs in all\n", n->id, NEOPIXEL_LEDS_TOTAL);
        return false;
    }
    if (!claim_pin(n->pin)) return false;
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(&ws2812_program, &st->pio, &st->sm, &st->offset,
                                                          n->pin, 1, true)) {
        printf("NeoPixels n%u: no PIO state machine free\n", n->id);
        used_pins &= ~(1u << n->pin);
        return false;
    }
    st->dma = dma_claim_unused_channel(false);
    if (st->dma < 0) {
        printf("NeoPixels n%u: no DMA channel free\n", n->id);
        pio_remove_program_and_unclaim_sm(&ws2812_program, st->pio, st->sm, st->offset);
        used_pins &= ~(1u << n->pin);
        return false;
    }
    ws2812_program_init(st->pio, st->sm, st->offset, n->pin, NEOPIXEL_BIT_HZ);

    dma_channel_config c = dma_channel_get_default_config((uint)st->dma);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(st->pio, st->sm, true));
    dma_channel_configure((uint)st->dma, &c, &st->pio->txf[st->sm], NULL, 0, false);

    st->leds = neopixel_pool + neopixel_pool_used;
    neopixel_pool_used += n->px.leds;
    printf("NeoPixels on GPIO %u: %u LEDs\n", n->pin, n->px.leds);
    return true;
}

// Whether a program has a NeoPixel node driving a pin.
static bool neopixel_on_pin(const hw_program *p, uint8_t pin) {
    for (uint8_t i = 0; i < p->node_count; i++) {
        if (p->nodes[i].type == MSG_HW_NEOPIXEL && !p->nodes[i].px_feeds && p->nodes[i].pin == pin) return true;
    }
    return false;
}

// Let the NeoPixel nodes of the program core 1 ran go: their state machines, programs
// and DMA channels. The LEDs of one whose pin `next` has no NeoPixels on are turned off
// first; the others are left showing what they did, so a save doesn't flicker them.
// Returns their pins, which release_pins then makes inputs: they are pulled down then, so
// the LEDs' data line doesn't float.
static uint32_t neopixels_release(const hw_program *p, const hw_program *next) {
    uint32_t pins = 0;
    for (uint8_t i = 0; i < p->node_count; i++) {
        const prog_node *n = &p->nodes[i];
        node_state *st = &states[i];
        if (n->type != MSG_HW_NEOPIXEL || !st->ok || n->px_feeds) continue;
        dma_channel_wait_for_finish_blocking((uint)st->dma);
        if (!neopixel_on_pin(next, n->pin)) {
            while (time_us_64() < st->ready_us) tight_loop_contents();
            for (uint16_t k = 0; k < n->px.leds; k++) pio_sm_put_blocking(st->pio, st->sm, 0);
            while (!pio_sm_is_tx_fifo_empty(st->pio, st->sm)) tight_loop_contents();
            busy_wait_us_32(NEOPIXEL_LED_US + NEOPIXEL_RESET_US);    // the last LED's bits, then the latch
        }
        dma_channel_unclaim((uint)st->dma);
        pio_sm_set_enabled(st->pio, st->sm, false);
        pio_remove_program_and_unclaim_sm(&ws2812_program, st->pio, st->sm, st->offset);
        pins |= 1u << n->pin;
    }
    neopixel_pool_used = 0;
    return pins;
}

// ── DS18B20 temperature sensors ──
// One sensor on a pin, on the 1-Wire bus: the line idles high through its pull-up, and
// the Pico or the sensor pulls it low. Core 1 drives it open-drain: the pin's output
// level stays 0, and is switched on (low) or off (let go). Each time slot is timed with
// interrupts off; between slots the line may idle as long as it likes, so a sensor is
// read a few bytes a scan, a scan taking at most about 2 ms for it.
#define OW_BYTES_A_SCAN     3       // scratchpad bytes read in one scan: ~1.7 ms
#define OW_RETRY_MS         1000    // a sensor that doesn't answer: look again after this

enum {
    OW_CONFIG,          // reset, skip ROM, write its resolution
    OW_CONVERT,         // reset, skip ROM, start a conversion
    OW_WAIT,            // until the conversion is done
    OW_READ_CMD,        // reset, skip ROM, read the scratchpad
    OW_READ,            // OW_BYTES_A_SCAN bytes of it a scan
    OW_IDLE,            // nothing answered: wait, then start again
};

// A reset pulse: low 480 µs, then the sensor's presence pulse. Whether one answered.
static bool ow_reset(uint pin) {
    uint32_t ints = save_and_disable_interrupts();
    gpio_set_dir(pin, GPIO_OUT);
    busy_wait_us_32(480);
    gpio_set_dir(pin, GPIO_IN);
    busy_wait_us_32(70);
    bool present = !gpio_get(pin);
    restore_interrupts(ints);
    busy_wait_us_32(410);
    return present;
}

// A write slot: low 6 µs for a 1, 60 µs for a 0, in a 70 µs slot.
static void ow_write_bit(uint pin, bool bit) {
    uint32_t ints = save_and_disable_interrupts();
    gpio_set_dir(pin, GPIO_OUT);
    busy_wait_us_32(bit ? 6 : 60);
    gpio_set_dir(pin, GPIO_IN);
    restore_interrupts(ints);
    busy_wait_us_32(bit ? 64 : 10);
}

// A read slot: low 3 µs, then the sensor holds the line low for a 0; sampled at 12 µs.
static bool ow_read_bit(uint pin) {
    uint32_t ints = save_and_disable_interrupts();
    gpio_set_dir(pin, GPIO_OUT);
    busy_wait_us_32(3);
    gpio_set_dir(pin, GPIO_IN);
    busy_wait_us_32(9);
    bool bit = gpio_get(pin);
    restore_interrupts(ints);
    busy_wait_us_32(55);
    return bit;
}

// Bytes go least significant bit first.
static void ow_write(uint pin, uint8_t byte) {
    for (int k = 0; k < 8; k++) ow_write_bit(pin, byte >> k & 1);
}

static uint8_t ow_read(uint pin) {
    uint8_t byte = 0;
    for (int k = 0; k < 8; k++) byte |= (uint8_t)(ow_read_bit(pin) << k);
    return byte;
}

// A read that failed, or no answer: after DS18B20_FAILS_LOST in a row it is lost, its
// found output goes off (its temperature keeps its last reading), and it is looked for
// again every OW_RETRY_MS.
static void ds18b20_failed(hw_program *p, const prog_node *n, node_state *st) {
    if (++st->ds.fails < DS18B20_FAILS_LOST) {
        st->ds.step = OW_CONVERT;
        return;
    }
    if (st->ds.found) printf("DS18B20 on GPIO %u lost\n", n->pin);
    st->ds.found = false;
    st->ds.configured = false;
    p->signals[n->out_count].value = 0;
    st->ds.step = OW_IDLE;
    st->ds.wait_ms = OW_RETRY_MS;
}

// One scan's part of reading a sensor: convert, wait, read the scratchpad, and again.
static void ds18b20_step(hw_program *p, const prog_node *n, node_state *st) {
    uint8_t res = n->options & DS18B20_RES_MASK;
    switch (st->ds.step) {
    case OW_IDLE:
        if ((st->ds.wait_ms -= SCAN_MS) <= 0) st->ds.step = OW_CONFIG;
        break;

    case OW_CONFIG:
        if (!ow_reset(n->pin)) { ds18b20_failed(p, n, st); break; }
        ow_write(n->pin, DS18B20_SKIP_ROM);
        ow_write(n->pin, DS18B20_WRITE_PAD);
        ow_write(n->pin, 0x4B);                 // TH and TL, the alarms: unused, its defaults
        ow_write(n->pin, 0x46);
        ow_write(n->pin, ds18b20_config(res));
        st->ds.configured = true;
        st->ds.step = OW_CONVERT;
        break;

    case OW_CONVERT:
        if (!st->ds.configured) { st->ds.step = OW_CONFIG; break; }
        if (!ow_reset(n->pin)) { ds18b20_failed(p, n, st); break; }
        ow_write(n->pin, DS18B20_SKIP_ROM);
        ow_write(n->pin, DS18B20_CONVERT);
        st->ds.wait_ms = ds18b20_conversion_ms(res);
        st->ds.step = OW_WAIT;
        break;

    case OW_WAIT:
        if ((st->ds.wait_ms -= SCAN_MS) <= 0) st->ds.step = OW_READ_CMD;
        break;

    case OW_READ_CMD:
        if (!ow_reset(n->pin)) { ds18b20_failed(p, n, st); break; }
        ow_write(n->pin, DS18B20_SKIP_ROM);
        ow_write(n->pin, DS18B20_READ_PAD);
        st->ds.got = 0;
        st->ds.step = OW_READ;
        break;

    case OW_READ: {
        for (int k = 0; k < OW_BYTES_A_SCAN && st->ds.got < DS18B20_PAD_LEN; k++)
            st->ds.pad[st->ds.got++] = ow_read(n->pin);
        if (st->ds.got < DS18B20_PAD_LEN) break;
        float celsius;
        if (!ds18b20_celsius(st->ds.pad, res, &celsius)) { ds18b20_failed(p, n, st); break; }
        st->ds.fails = 0;
        st->ds.step = OW_CONVERT;
        if (!ds18b20_take(celsius, st->ds.found, p->signals[n->out].value, &st->ds.doubted)) break;
        if (!st->ds.found) printf("DS18B20 on GPIO %u: %.2f C\n", n->pin, (double)celsius);
        p->signals[n->out].value = celsius;
        p->signals[n->out_count].value = 1;
        st->ds.found = true;
        break;
    }
    }
}

// ── Rotary encoders ──
// An encoder's A and B pins change faster than the scan when it is turned quickly, so
// core 1 takes each of their edges with an interrupt: encoders_irq decodes them into
// clicks (encoder_decode), and each scan adds the clicks since the last to its
// position. Its switch is read each scan, debounced as a GPIO input. Its pins have
// their pull-ups on: a KY-040 has resistors on A and B, but often none on its switch.
#define ENCODERS_MAX        8
#define ENCODER_EDGES       (GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE)

static struct {
    uint8_t a, b;
    uint8_t detent;             // ENCODER_DETENT_
    encoder_decoder decoder;
    volatile int32_t clicks;    // since the scan last took them
} encoders[ENCODERS_MAX];
static volatile uint8_t encoders_on = 0;

static inline uint8_t encoder_pins(uint8_t a, uint8_t b) {
    return (uint8_t)(gpio_get(a) << 1 | gpio_get(b));
}

// Part of IO_IRQ_BANK0 on core 1 (pins_irq), shared with core 0's handlers (the WiFi
// chip's): each encoder whose pins have an edge for this core takes it. It reads the
// pins after acknowledging their edges, so an edge after the read raises it again.
static inline __attribute__((always_inline)) void encoders_irq(void) {
    for (uint8_t i = 0; i < encoders_on; i++) {
        uint32_t a = gpio_get_irq_event_mask(encoders[i].a) & ENCODER_EDGES;
        uint32_t b = gpio_get_irq_event_mask(encoders[i].b) & ENCODER_EDGES;
        if (!a && !b) continue;
        if (a) gpio_acknowledge_irq(encoders[i].a, a);
        if (b) gpio_acknowledge_irq(encoders[i].b, b);
        encoders[i].clicks += encoder_decode(&encoders[i].decoder, encoder_pins(encoders[i].a, encoders[i].b),
                                             encoders[i].detent);
    }
}

// Stop taking the edges of the last program's encoders, before their pins are released.
static void encoders_stop(void) {
    uint32_t ints = save_and_disable_interrupts();
    for (uint8_t i = 0; i < encoders_on; i++) {
        gpio_set_irq_enabled(encoders[i].a, ENCODER_EDGES, false);
        gpio_set_irq_enabled(encoders[i].b, ENCODER_EDGES, false);
    }
    encoders_on = 0;
    restore_interrupts(ints);
}

static void encoder_pin_init(uint8_t pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
}

// Its pins, all or none, and a place in encoders[]. It starts where the program
// before left it (take_program kept its position), within its steps.
static bool encoder_setup(hw_program *p, const prog_node *n, node_state *st) {
    if (encoders_on == ENCODERS_MAX) {
        printf("encoder n%u: more than %u encoders\n", n->id, ENCODERS_MAX);
        return false;
    }
    uint8_t pins = n->enc_pins[2] == ENCODER_NO_BUTTON ? 2 : 3;
    for (uint8_t k = 0; k < pins; k++) {
        if (claim_pin(n->enc_pins[k])) continue;
        while (k--) used_pins &= ~(1u << n->enc_pins[k]);
        return false;
    }
    for (uint8_t k = 0; k < pins; k++) encoder_pin_init(n->enc_pins[k]);
    busy_wait_us_32(10);        // the pull-ups bring the pins up

    int32_t at = (int32_t)lroundf(p->signals[n->out].value);
    p->signals[n->out].value = (float)encoder_position(at, 0, n->enc_steps, n->options & ENCODER_WRAP);
    p->signals[n->out_count].value = 0;

    uint8_t i = encoders_on;
    encoders[i].a = n->enc_pins[0];
    encoders[i].b = n->enc_pins[1];
    encoders[i].detent = n->options & ENCODER_DETENT_MASK;
    encoders[i].clicks = 0;
    encoder_start(&encoders[i].decoder, encoder_pins(n->enc_pins[0], n->enc_pins[1]));
    st->encoder = i;
    uint32_t ints = save_and_disable_interrupts();
    encoders_on = i + 1;
    gpio_set_irq_enabled(n->enc_pins[0], ENCODER_EDGES, true);    // acknowledges an old edge first
    gpio_set_irq_enabled(n->enc_pins[1], ENCODER_EDGES, true);
    restore_interrupts(ints);
    printf("encoder on GPIO %u and %u\n", n->enc_pins[0], n->enc_pins[1]);
    return true;
}

// One scan of an encoder: the clicks since the last move its position; its switch,
// held in its new state GPI_DEBOUNCE reads in a row, is pressed while low.
static void encoder_scan(hw_program *p, const prog_node *n, node_state *st) {
    uint32_t ints = save_and_disable_interrupts();
    int32_t clicks = encoders[st->encoder].clicks;
    encoders[st->encoder].clicks = 0;
    restore_interrupts(ints);
    if (n->options & ENCODER_REVERSE) clicks = -clicks;
    if (clicks) {
        int32_t at = (int32_t)lroundf(p->signals[n->out].value);
        p->signals[n->out].value = (float)encoder_position(at, clicks, n->enc_steps, n->options & ENCODER_WRAP);
    }

    if (n->enc_pins[2] == ENCODER_NO_BUTTON) return;
    bool pressed = !gpio_get(n->enc_pins[2]);
    if (pressed == st->on) {
        st->differ = 0;
    } else if (++st->differ >= GPI_DEBOUNCE) {
        st->on = pressed;
        st->differ = 0;
        p->signals[n->out_count].value = pressed;
    }
}

// ── Ultrasonic rangers ──
// A ranger whose trigger and echo share its pin (Seeed's Grove Ultrasonic Ranger): core 1
// pulses the pin high for ULTRASONIC_TRIGGER_US and lets it go, and the ranger answers
// with a pulse as long as its sound took there and back, up to about 25 ms. That is
// longer than a scan, so the pulse's edges are timed with an interrupt, as an encoder's
// are taken. A ping goes out every ULTRASONIC_PING_MS, the rangers in turn, so one
// doesn't hear another's. Between pings the pin is an input with its pull-down on, which
// holds the line low while neither end drives it.
#define ULTRASONICS_MAX         4
#define ULTRASONIC_PING_SCANS   (ULTRASONIC_PING_MS / SCAN_MS)

enum {
    US_IDLE,            // no ping out
    US_SENT,            // triggered: waiting for its pulse to start
    US_HIGH,            // its pulse is high
    US_DONE,            // its pulse ended: pulse_us is its length
};

static struct {
    uint8_t pin;
    volatile uint8_t stage;     // US_
    volatile uint64_t rise_us;  // when its pulse started
    volatile uint32_t pulse_us;
} ultrasonics[ULTRASONICS_MAX];
static volatile uint8_t ultrasonics_on = 0;

// The edges of a ranger's echo pulse, on core 1: when it rose, then how long it was. A
// pulse so short both edges are waiting counts as length 0, which is no echo.
static inline __attribute__((always_inline)) void ultrasonics_irq(void) {
    for (uint8_t i = 0; i < ultrasonics_on; i++) {
        uint32_t edges = gpio_get_irq_event_mask(ultrasonics[i].pin) & ENCODER_EDGES;
        if (!edges) continue;
        gpio_acknowledge_irq(ultrasonics[i].pin, edges);
        uint64_t now = time_us_64();
        if (ultrasonics[i].stage == US_SENT && (edges & GPIO_IRQ_EDGE_RISE)) {
            ultrasonics[i].rise_us = now;
            ultrasonics[i].stage = US_HIGH;
        }
        if (ultrasonics[i].stage == US_HIGH && (edges & GPIO_IRQ_EDGE_FALL)) {
            ultrasonics[i].pulse_us = (uint32_t)(now - ultrasonics[i].rise_us);
            ultrasonics[i].stage = US_DONE;
        }
    }
}

// IO_IRQ_BANK0 on core 1: the encoders' and the rangers' pins.
static void __not_in_flash_func(pins_irq)(void) {
    encoders_irq();
    ultrasonics_irq();
}

// Drop the pings that are out: each counts as one without an echo. After core 1 was
// paused with its interrupts off, when a pulse's edges were taken late.
static void ultrasonics_drop(void) {
    for (uint8_t i = 0; i < ultrasonics_on; i++) {
        gpio_set_irq_enabled(ultrasonics[i].pin, ENCODER_EDGES, false);
        ultrasonics[i].stage = US_IDLE;
    }
}

// Stop timing the last program's rangers, before their pins are released.
static void ultrasonics_stop(void) {
    uint32_t ints = save_and_disable_interrupts();
    for (uint8_t i = 0; i < ultrasonics_on; i++) gpio_set_irq_enabled(ultrasonics[i].pin, ENCODER_EDGES, false);
    ultrasonics_on = 0;
    restore_interrupts(ints);
}

// Its pin, and a place in ultrasonics[]. With an echo from the program before
// (take_program kept its outputs) it goes on from that distance, if within its range;
// otherwise it starts with no echo, at its range.
static bool ultrasonic_setup(hw_program *p, const prog_node *n, node_state *st) {
    if (ultrasonics_on == ULTRASONICS_MAX) {
        printf("ultrasonic n%u: more than %u rangers\n", n->id, ULTRASONICS_MAX);
        return false;
    }
    if (!claim_pin(n->pin)) return false;
    gpio_init(n->pin);
    gpio_put(n->pin, 0);
    gpio_set_dir(n->pin, GPIO_IN);
    gpio_pull_down(n->pin);

    if (p->signals[n->out_count].value < 0.5f || p->signals[n->out].value > (float)n->range) {
        p->signals[n->out].value = (float)n->range;
        p->signals[n->out_count].value = 0;
    }

    uint8_t i = ultrasonics_on;
    ultrasonics[i].pin = n->pin;
    ultrasonics[i].stage = US_IDLE;
    st->ultrasonic = i;
    uint32_t ints = save_and_disable_interrupts();
    ultrasonics_on = i + 1;
    restore_interrupts(ints);
    printf("ultrasonic ranger on GPIO %u\n", n->pin);
    return true;
}

// Send a ping: the pin low, high for the trigger, low, then let go for the ranger to
// answer on. Its edges are taken from here; the trigger's own are cleared first.
static void ultrasonic_ping(uint8_t i) {
    uint8_t pin = ultrasonics[i].pin;
    gpio_set_irq_enabled(pin, ENCODER_EDGES, false);
    gpio_put(pin, 0);
    gpio_set_dir(pin, GPIO_OUT);
    busy_wait_us_32(2);
    gpio_put(pin, 1);
    busy_wait_us_32(ULTRASONIC_TRIGGER_US);
    gpio_put(pin, 0);
    busy_wait_us_32(2);
    gpio_set_dir(pin, GPIO_IN);
    ultrasonics[i].stage = US_SENT;
    gpio_set_irq_enabled(pin, ENCODER_EDGES, true);     // acknowledges the trigger's edges first
}

// One scan of a ranger. Its ping's outcome is taken once its pulse has ended, or when
// the next ping is due with no pulse, or one still high: no echo. Each
// ULTRASONIC_PING_SCANS scans one ranger pings, in turn.
static void ultrasonic_scan(hw_program *p, const prog_node *n, node_state *st, uint32_t count) {
    uint8_t i = st->ultrasonic;
    bool due = count % ULTRASONIC_PING_SCANS == 0;
    if (st->on && (ultrasonics[i].stage == US_DONE || due)) {
        gpio_set_irq_enabled(n->pin, ENCODER_EDGES, false);
        bool answered = ultrasonics[i].stage == US_DONE;
        ultrasonics[i].stage = US_IDLE;
        float cm = p->signals[n->out].value;
        bool echo = p->signals[n->out_count].value >= 0.5f;
        ultrasonic_take(answered, ultrasonics[i].pulse_us, n->range, &cm, &echo, &st->differ);
        p->signals[n->out].value = cm;
        p->signals[n->out_count].value = echo;
        st->on = false;
    }
    if (due && (count / ULTRASONIC_PING_SCANS) % ultrasonics_on == i) {
        ultrasonic_ping(i);
        st->on = true;
    }
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

    case MSG_HW_DS18B20:
        // The line idles high: through the 4.7 kΩ resistor, or the pin's own pull-up.
        // The output level stays low, for ow_ to pull the line down with.
        if (!claim_pin(n->pin)) return false;
        gpio_init(n->pin);
        gpio_put(n->pin, 0);
        gpio_set_dir(n->pin, GPIO_IN);
        if (n->options & DS18B20_PULLUP) gpio_pull_up(n->pin);
        else gpio_disable_pulls(n->pin);
        return true;

    case MSG_HW_ENCODER:
        return encoder_setup(p, n, st);

    case MSG_HW_ULTRASONIC:
        return ultrasonic_setup(p, n, st);

    case MSG_HW_GPO:
        if (!claim_pin(n->pin)) return false;
        gpio_init(n->pin);
        gpio_set_dir(n->pin, GPIO_OUT);
        return true;

    case MSG_HW_PWM:
    case MSG_HW_SERVO:
        return claim_pin(n->pin);       // its slice starts at the first scan

    case MSG_HW_NEOPIXEL:
        return neopixel_setup(n, st);

    case MSG_HW_STEPPER:
        // Its four pins, or none: a stepper missing a coil can't turn.
        for (int k = 0; k < STEPPER_PINS; k++) {
            if (claim_pin(n->coil_pins[k])) continue;
            while (k--) used_pins &= ~(1u << n->coil_pins[k]);
            return false;
        }
        for (int k = 0; k < STEPPER_PINS; k++) {
            gpio_init(n->coil_pins[k]);
            gpio_set_dir(n->coil_pins[k], GPIO_OUT);
        }
        st->motion = stepper_motion_of(n);
        return true;

    default:
        return true;
    }
}

// ── Stepper outputs ──
// A stepper's coils, bit k for IN(k + 1), on its pins.
static void stepper_put(const prog_node *n, uint8_t coils) {
    for (int k = 0; k < STEPPER_PINS; k++) gpio_put(n->coil_pins[k], coils >> k & 1);
}

// Run each moving stepper's motion on to now, and step it when it is due. A stepper
// steps faster than the scan, so core 1 runs this between scans as well; it is
// still once it has settled, and then turns its coils off with STEPPER_RELEASE.
static void steppers_run(void) {
    hw_program *p = &programs[running];
    uint64_t now = time_us_64();
    for (uint8_t i = 0; i < p->node_count; i++) {
        const prog_node *n = &p->nodes[i];
        node_state *st = &states[i];
        if (n->type != MSG_HW_STEPPER || !st->ok || !st->stepper.moving) continue;
        float dt = (float)(now - st->stepped_us) / 1e6f;
        if (dt > 0.05f) dt = 0.05f;     // after a pause for a flash write: as if it had been short
        st->stepped_us = now;
        if (stepper_advance(n, &st->motion, &st->stepper, st->in, dt)) stepper_put(n, stepper_coils(n, st->stepper.pos));
        if (!st->stepper.moving && (n->options & STEPPER_RELEASE)) stepper_put(n, 0);
    }
}

// Microseconds until a moving stepper next needs steppers_run, at most `most`.
static int64_t steppers_wait_us(int64_t most) {
    const hw_program *p = &programs[running];
    uint64_t now = time_us_64();
    for (uint8_t i = 0; i < p->node_count; i++) {
        const node_state *st = &states[i];
        if (p->nodes[i].type != MSG_HW_STEPPER || !st->ok || !st->stepper.moving) continue;
        int64_t due = (int64_t)(st->stepped_us + (uint64_t)(stepper_wait(&st->stepper) * 1e6f)) - (int64_t)now;
        if (due < most) most = due;
    }
    return most;
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

    // A UI source's signals keep their values, so do a DS18B20's and an ultrasonic
    // ranger's until it is read again, and an encoder's position, so a save neither
    // moves a slider or a knob's position nor drops a reading.
    for (uint8_t i = 0; i < p->signal_count; i++) {
        prog_signal *s = &p->signals[i];
        if (!program_ui_source(s->type) && s->type != MSG_HW_DS18B20 && s->type != MSG_HW_ULTRASONIC
            && !(s->type == MSG_HW_ENCODER && s->output == 0)) continue;
        uint8_t was = program_signal(old, s->id, s->output);
        if (was != PROG_NO_SIGNAL) s->value = old->signals[was].value;
    }

    // A stepper keeps where it is, in revolutions, so a save doesn't move it: that of
    // the stepper with its ID in the program core 1 ran.
    float turned[PROG_NODES_MAX] = { 0 };
    for (uint8_t j = 0; j < p->node_count; j++) {
        if (p->nodes[j].type != MSG_HW_STEPPER) continue;
        for (uint8_t i = 0; i < old->node_count; i++) {
            if (old->nodes[i].type != MSG_HW_STEPPER || old->nodes[i].id != p->nodes[j].id || !states[i].ok) continue;
            turned[j] = (float)states[i].stepper.pos / states[i].motion.steps_rev;
        }
    }

    uint32_t neopixel_pins = neopixels_release(old, p);
    encoders_stop();
    ultrasonics_stop();
    release_pins();
    for (uint gpio = 0; gpio < 32; gpio++) if (neopixel_pins >> gpio & 1) gpio_pull_down(gpio);
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
        if (n->type == MSG_HW_STEPPER && states[i].ok)
            states[i].stepper.pos = (int32_t)lroundf(turned[i] * states[i].motion.steps_rev);
    }
    printf("hw man running a program of %u signals and %u nodes\n", p->signal_count, p->node_count);
}

// Show the NeoPixels of chain ending in node `last`, the one that drives the LEDs:
// each node of it, from the first (the one with nothing on its data line in), draws its
// graphic over what the one before it drew. Sent again when what any of them shows
// changes, once the last send is out; if it isn't yet, at a later scan.
static void neopixels_show(hw_program *p, uint8_t last) {
    node_state *st = &states[last];
    uint8_t chain[PROG_NODES_MAX], len = 0;
    for (uint8_t k = last; k != PROG_NO_NODE && len < PROG_NODES_MAX; k = p->nodes[k].px_under) chain[len++] = k;

    bool changed = !st->driven;
    for (uint8_t c = 0; c < len && !changed; c++) {
        const prog_node *m = &p->nodes[chain[c]];
        changed = neopixel_fraction(m, signal_value(p, m->in[0])) != states[chain[c]].fraction
            || neopixel_enabled(m, signal_value(p, m->in[1])) != states[chain[c]].on;
    }
    if (!changed) return;
    uint64_t now = time_us_64();
    if (now < st->ready_us || dma_channel_is_busy((uint)st->dma)) return;

    const prog_node *n = &p->nodes[last];
    memset(st->leds, 0, n->px.leds * sizeof st->leds[0]);
    for (uint8_t c = len; c-- > 0;) {
        const prog_node *m = &p->nodes[chain[c]];
        node_state *ms = &states[chain[c]];
        ms->fraction = neopixel_fraction(m, signal_value(p, m->in[0]));
        ms->on = neopixel_enabled(m, signal_value(p, m->in[1]));
        neopixel_draw(&m->px, ms->fraction, ms->on, st->leds);
    }
    dma_channel_transfer_from_buffer_now((uint)st->dma, st->leds, n->px.leds);
    st->ready_us = now + (uint64_t)n->px.leds * NEOPIXEL_LED_US + NEOPIXEL_RESET_US;
    st->driven = true;
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
            if (n->out_count != PROG_NO_SIGNAL) p->signals[n->out_count].value = (float)st->logic.count;
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

        case MSG_HW_DS18B20:
            ds18b20_step(p, n, st);
            break;

        case MSG_HW_CLOCK: {
            uint8_t parts[3];
            clock_parts(clock_now(), parts);
            for (uint8_t k = 0; k < 3; k++) p->signals[n->out + k].value = parts[k];
            break;
        }

        case MSG_HW_ENCODER:
            encoder_scan(p, n, st);
            break;

        case MSG_HW_ULTRASONIC:
            ultrasonic_scan(p, n, st, count);
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

        case MSG_HW_SERVO: {
            // Its slice runs at SERVO_FREQ; the pulse is the level, as a share of the
            // frame. At its first scan it goes straight to its angle: where it was is
            // not known.
            float target = servo_target(n, signal_value(p, n->in[0]));
            float angle = st->driven ? servo_move(n, st->angle, target, SCAN_MS / 1000.0f) : target;
            if (st->driven && angle == st->angle) break;
            if (!st->driven) {
                printf("servo on GPIO %u\n", n->pin);
                st->top = pwm_set_freq(n->pin, SERVO_FREQ);
            }
            float level = st->top * servo_pulse_us(n, angle) * SERVO_FREQ / 1e6f + 0.5f;
            pwm_set_gpio_level(n->pin, (uint16_t)(level > st->top ? st->top : level));
            st->angle = angle;
            st->driven = true;
            break;
        }

        case MSG_HW_NEOPIXEL:
            if (!n->px_feeds) neopixels_show(p, i);   // one that feeds another, its chain's last shows
            break;

        case MSG_HW_STEPPER: {
            // The scan gives it its input; steppers_run moves it.
            st->in = signal_value(p, n->in[0]);
            if (!st->driven) {
                // Its coils hold it from the start, unless it lets them go when still.
                if (!(n->options & STEPPER_RELEASE)) stepper_put(n, stepper_coils(n, st->stepper.pos));
                st->driven = true;
            }
            if (!st->stepper.moving && !stepper_settled(n, &st->motion, &st->stepper, st->in)) {
                stepper_put(n, stepper_coils(n, st->stepper.pos));
                st->stepper.moving = true;
                st->stepped_us = time_us_64();
            }
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

    // The encoders' and the ultrasonic rangers' pin edges, on this core.
    irq_add_shared_handler(IO_IRQ_BANK0, pins_irq, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(IO_IRQ_BANK0, true);

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

        steppers_run();
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

        // Until the next scan, or a stepper's next step if that is sooner, take core
        // 0's words: each is an action word.
        int64_t wait = steppers_wait_us(absolute_time_diff_us(get_absolute_time(), next_scan));
        uint32_t op;
        if (multicore_fifo_pop_timeout_us(wait > 0 ? (uint64_t)wait : 0, &op)) {
            if (fifo_op(op) == FIFO_OP_CONTROL_ACTION) command(op);
        }
    }
}
