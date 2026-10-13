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

// Constants for conversion
#define ADC_TEMP_SENSOR 4        // Internal temperature sensor is connected to ADC 4
#define ADC_VREF 3.3             // Reference voltage (3.3V)
#define ADC_RESOLUTION 4095.0    // 12-bit ADC resolution (2^12 - 1)
#define TEMP_OFFSET 27.0         // Calibration temperature (27°C)
#define TEMP_SLOPE -1.721        // mV per degree change (from datasheet)

float read_internal_temperature() {
    adc_select_input(ADC_TEMP_SENSOR);
    sleep_us(10);  // Allow ADC to stabilize
    uint16_t raw = adc_read();

    float voltage = (raw * 3.3f) / 4095.0f; // Convert ADC to voltage
    float temperature = 27.0f - ((voltage - 0.706f) / 0.001721f);
    
    return temperature;
}

#define PWM_SLICE_NUM 12

typedef struct {
    uint8_t operation;
    uint8_t control_type;
    uint16_t id;
    union {
        uint8_t io;
        uint8_t value;
    };
    uint16_t source_id;
    uint8_t drives;     // PWM: the parameter this source drives, PWM_DRIVES_DUTY or _FREQ
} hwel;

typedef struct {
    uint16_t id;
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

    return result;
}

void decode_hwrf(uint32_t rf,hwel *decop) {
    decop->source_id = fifo_ref_source(rf);
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

void add_packet(hwel el) {
    // First check if one with the same ID already exists
    hwel_sources *existing = NULL;
    HASH_FIND(hh, hwel_source_table, &el.source_id, sizeof(uint16_t), existing);
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
        printf("adding another hwel allocation to src_id:%d, type:%02X, id:%d\n",existing->id, el.control_type,el.id);
        existing->len++;
        existing->hwels[existing->len-1] = el;

        return;
    }

    printf("new hwel_sources allocation, src_id:%d\n",el.source_id);
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
    new_srcsel->id = el.source_id;

    HASH_ADD(hh, hwel_source_table, id, sizeof(uint16_t), new_srcsel);
}

hwel_sources *find_packet(uint16_t id) {
    hwel_sources *srcsel = NULL;
    HASH_FIND(hh, hwel_source_table, &id, sizeof(uint16_t), srcsel);
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

void core1_worker() {
    printf("(core1) started\n");
    
    adc_init();
    adc_set_temp_sensor_enabled(true);

    absolute_time_t start = get_absolute_time();
    uint64_t next_uptime_timeout = 5;
    uint64_t next_coretm_timeout = 6;

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

        uint32_t op;
        if (multicore_fifo_pop_timeout_us(1000, &op)) {
            hwel decop = decode_hwel(op);
            // printf("hw opp %d type %d, io %d\n",decop.operation, decop.control_type, decop.io);
            
            switch (decop.operation) {
            case FIFO_OP_SETUP:
                switch (decop.control_type) {
                case MSG_HW_LED:
                    multicore_fifo_pop_timeout_us(1000, &op);
                    decode_hwrf(op,&decop);
                    break;

                case MSG_HW_GPO:
                    multicore_fifo_pop_timeout_us(1000, &op);
                    decode_hwrf(op,&decop);

                    gpio_init(decop.io);
                    gpio_set_dir(decop.io, GPIO_OUT);
                    break;
                
                case MSG_HW_PWM: {
                    // Reference (duty source), PWM, duty, reference (frequency
                    // source) and frequency words follow.
                    uint32_t w[5] = {0};
                    for (int i = 0; i < 5; i++) multicore_fifo_pop_timeout_us(1000, &w[i]);
                    decode_hwrf(w[0], &decop);

                    uint pin = decop.io;
                    if (pin >= PWM_PINS) break;
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
                    freq_el.source_id = fifo_ref_source(w[3]);
                    if (freq_el.source_id) add_packet(freq_el);
                    break;
                }

                default:
                    break;
                }

                add_packet(decop);
                printf("hw man adding control type %02X on ID %i\n",decop.control_type,decop.id);
                break;

            case FIFO_OP_CLEAN_SETUP:
                delete_all_packets();
                memset(pwm_outs, 0, sizeof pwm_outs);
                for (uint gpio = 0; gpio < 30; gpio++) {
                    gpio_set_function(gpio, GPIO_FUNC_SIO);
                    gpio_set_dir(gpio, GPIO_IN);
                    gpio_disable_pulls(gpio);
                }
                for (uint slice_num = 0; slice_num < PWM_SLICE_NUM; slice_num++) {
                    pwm_set_enabled(slice_num, false);
                    pwm_set_wrap(slice_num, 0xFFFF);
                    pwm_set_chan_level(slice_num, PWM_CHAN_A, 0);
                    pwm_set_chan_level(slice_num, PWM_CHAN_B, 0);
                }
            case FIFO_OP_CONTROL_ACTION:
                // msg = multicore_fifo_pop_blocking();
                printf("hw man buffer %02X %02X %02X %02X\n",op>>24&0xFF,op>>16&0xFF,op>>8&0xFF,op&0xFF);
                // hwop decop = decode_hwop(msg);
                
                // uint32_t values[decop.n_values];
                // for (size_t iValue = 0; iValue < decop.n_values; iValue++) {
                //     values[iValue] = multicore_fifo_pop_blocking();
                // }

                printf("hw man finding ID %d\n",decop.id);
                
                hwel_sources *hw_srcs = find_packet(decop.id);
                if (hw_srcs) {
                    for (size_t i = 0; i < hw_srcs->len; i++) {
                        hwel el = hw_srcs->hwels[i];
    
                        printf("hw man found type %02X with ID %d\n",el.control_type,el.id);
                        switch (el.control_type) {
                        case MSG_HW_LED:
                            printf("switch LED, value:%i\n",decop.value);
                            push_to_core0(FIFO_TAG_LED);
                            push_to_core0(decop.value >= VALUE_HALF);
                            break;
                        
                        case MSG_HW_GPO:
                            printf("switch GPO %d, value:%i\n",el.io, decop.value);
                            gpio_put(el.io, decop.value >= VALUE_HALF);
                            break;
                        
                        case MSG_HW_PWM:
                            printf("switch PWM %d, value:%i\n",el.io, decop.value);
                            if (el.io >= PWM_PINS) break;
                            if (el.drives == PWM_DRIVES_FREQ) pwm_outs[el.io].freq_in = decop.value;
                            else pwm_outs[el.io].duty_in = decop.value;
                            pwm_apply(el.io);
                            break;

                        default:
                            break;
                        }
                    }
                } else {
                    printf("hw sources with ID %d not found\n",decop.id);
                }
            }
        }

        int64_t core_time_elapsed = absolute_time_diff_us(start, get_absolute_time())/1200000;
        if (core_time_elapsed >= next_coretm_timeout) {
            float temperature = read_internal_temperature();
            push_to_core0(FIFO_TAG_TEMPERATURE);
            uint32_t data;
            memcpy(&data, &temperature, sizeof(float));
            push_to_core0(data);
            next_coretm_timeout = core_time_elapsed + 5;
        }
    }
}