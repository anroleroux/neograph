#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "program.h"
#include "protocol.h"
#include "config/config_iterator.h"

bool program_ui_source(uint8_t type) {
    return type == MSG_UI_BUTTON || type == MSG_UI_SLIDER || type == MSG_UI_SLIDER_2D;
}

uint8_t program_signal(const hw_program *p, uint16_t id, uint8_t output) {
    for (uint8_t i = 0; i < p->signal_count; i++) {
        if (p->signals[i].id == id && p->signals[i].output == output) return i;
    }
    return PROG_NO_SIGNAL;
}

// The signal of an output of a source, added if it is new, with its starting
// value. A source whose ID was seen before (configs from older UIs repeat IDs;
// the last message wins) keeps its signal. PROG_NO_SIGNAL if there is no room.
static uint8_t add_signal(hw_program *p, uint16_t id, uint8_t output, uint8_t type, float value) {
    uint8_t s = program_signal(p, id, output);
    if (s == PROG_NO_SIGNAL) {
        if (p->signal_count == PROG_SIGNALS_MAX) return PROG_NO_SIGNAL;
        s = p->signal_count++;
    }
    p->signals[s] = (prog_signal){ .id = id, .output = output, .type = type, .value = value };
    return s;
}

static prog_node *add_node(hw_program *p, uint8_t type, uint16_t id) {
    if (p->node_count == PROG_NODES_MAX) return NULL;
    prog_node *n = &p->nodes[p->node_count++];
    *n = (prog_node){ .type = type, .id = id, .out = PROG_NO_SIGNAL,
                      .in = { PROG_NO_SIGNAL, PROG_NO_SIGNAL }, .in_full = { 1, 1 } };
    return n;
}

// The value of a source's signal that drives an output to its max, such as a
// PWM's: 1 for a slider (its values are 0 to 1), 3.3 V for an ADC, CORE_TEMP_FULL
// °C for the core temperature.
static float signal_full(const hw_program *p, uint8_t s) {
    if (s == PROG_NO_SIGNAL) return 1;
    switch (p->signals[s].type) {
    case MSG_HW_ADC: return PROG_ADC_VOLTS;
    case MSG_HW_CORE: return CORE_TEMP_FULL;
    default: return 1;
    }
}

static uint8_t ref_signal(const hw_program *p, msg_ref ref) {
    return ref.id ? program_signal(p, ref.id, ref.output) : PROG_NO_SIGNAL;
}

void program_compile(const uint8_t *payload, size_t len, hw_program *p) {
    p->signal_count = 0;
    p->node_count = 0;

    config_element_iterator itr;
    config_element_iterator_init(&itr, payload, len);

    // The inputs: a signal for each output of each source, and a node for each
    // hardware input, which writes its signal.
    while (itr.next(&itr)) {
        prog_node *n;
        uint8_t s;
        switch (itr.type) {
        case MSG_UI_BUTTON:
        case MSG_UI_SLIDER:
            add_signal(p, itr.id, 0, itr.type, 0);
            break;

        case MSG_UI_SLIDER_2D: {
            // Its outputs start at its home, where the puck starts in the browser.
            uint8_t home[2];
            slider_2d_home(itr.pay, itr.len, home);
            for (uint8_t i = 0; i < 2; i++) add_signal(p, itr.id, i, itr.type, (float)home[i] / VALUE_FULL);
            break;
        }

        case MSG_HW_CORE:
            if (itr.len != 0) break;
            if ((s = add_signal(p, itr.id, 0, itr.type, 0)) == PROG_NO_SIGNAL) break;
            if ((n = add_node(p, itr.type, itr.id))) n->out = s;
            break;

        case MSG_HW_GPI:
            if (itr.len != 2) break;
            if ((s = add_signal(p, itr.id, 0, itr.type, 0)) == PROG_NO_SIGNAL) break;
            if ((n = add_node(p, itr.type, itr.id))) {
                n->out = s;
                n->pin = itr.pay[0];
                n->options = itr.pay[1];
            }
            break;

        case MSG_HW_ADC:
            if (itr.len != 1 || itr.pay[0] >= ADC_CHANNELS) break;
            if ((s = add_signal(p, itr.id, 0, itr.type, 0)) == PROG_NO_SIGNAL) break;
            if ((n = add_node(p, itr.type, itr.id))) {
                n->out = s;
                n->pin = itr.pay[0];
            }
            break;

        default:
            break;
        }
    }

    // The outputs, after every signal they may read.
    itr.reset(&itr);
    while (itr.next(&itr)) {
        prog_node *n;
        uint8_t r = msg_ref_len(itr.flags);
        switch (itr.type) {
        case MSG_HW_LED:
            if (itr.len != r) break;
            if ((n = add_node(p, itr.type, itr.id))) n->in[0] = ref_signal(p, msg_ref_at(itr.pay, itr.flags));
            break;

        case MSG_HW_GPO:
            if (itr.len != r + 1) break;
            if ((n = add_node(p, itr.type, itr.id))) {
                n->in[0] = ref_signal(p, msg_ref_at(itr.pay, itr.flags));
                n->pin = itr.pay[r];
            }
            break;

        case MSG_HW_KEYBOARD:
            // Core 0 keeps the keystroke; core 1 says when its input goes on.
            if (!keyboard_payload_valid(itr.pay, itr.len, itr.flags)) break;
            if ((n = add_node(p, itr.type, itr.id))) n->in[0] = ref_signal(p, msg_ref_at(itr.pay, itr.flags));
            break;

        case MSG_HW_PWM: {
            pwm_payload pwm;
            if (!pwm_payload_read(itr.pay, itr.len, itr.flags, &pwm)) break;
            if (!(n = add_node(p, itr.type, itr.id))) break;
            n->pin = pwm.pin;
            n->options = pwm.options;
            n->duty = pwm.duty;
            n->duty_min = pwm.duty_min;
            n->duty_max = pwm.duty_max;
            n->freq = pwm.freq;
            n->freq_min = pwm.freq_min;
            n->freq_max = pwm.freq_max;
            n->in[0] = ref_signal(p, pwm.duty_source);
            n->in[1] = ref_signal(p, pwm.freq_source);
            for (int i = 0; i < 2; i++) n->in_full[i] = signal_full(p, n->in[i]);
            break;
        }

        default:
            break;
        }
    }
}

// ── Description ──

static const char *type_name(uint8_t type) {
    switch (type) {
    case MSG_UI_BUTTON: return "button";
    case MSG_UI_SLIDER: return "slider";
    case MSG_UI_SLIDER_2D: return "slider-2d";
    case MSG_HW_CORE: return "core";
    case MSG_HW_GPI: return "gpi";
    case MSG_HW_ADC: return "adc";
    default: return "?";
    }
}

typedef struct {
    char *buf;
    size_t size, len;
} text;

static void put(text *t, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void put(text *t, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    size_t room = t->len < t->size ? t->size - t->len : 0;
    int n = vsnprintf(room ? t->buf + t->len : NULL, room, fmt, ap);
    va_end(ap);
    if (n > 0) t->len += (size_t)n;
}

static void put_signal(text *t, uint8_t s) {
    if (s == PROG_NO_SIGNAL) put(t, "-");
    else put(t, "s%u", s);
}

size_t program_describe(const hw_program *p, char *buf, size_t size) {
    text t = { buf, size, 0 };
    if (size) buf[0] = 0;

    for (uint8_t i = 0; i < p->signal_count; i++) {
        const prog_signal *s = &p->signals[i];
        put(&t, "s%u n%u.%u %s = %.4g\n", i, s->id, s->output, type_name(s->type), (double)s->value);
    }
    for (uint8_t i = 0; i < p->node_count; i++) {
        const prog_node *n = &p->nodes[i];
        switch (n->type) {
        case MSG_HW_CORE: put(&t, "CORE n%u -> ", n->id); put_signal(&t, n->out); break;
        case MSG_HW_ADC: put(&t, "ADC n%u channel %u -> ", n->id, n->pin); put_signal(&t, n->out); break;
        case MSG_HW_GPI: put(&t, "GPI n%u pin %u options %02X -> ", n->id, n->pin, n->options); put_signal(&t, n->out); break;
        case MSG_HW_LED: put(&t, "LED n%u <- ", n->id); put_signal(&t, n->in[0]); break;
        case MSG_HW_GPO: put(&t, "GPO n%u pin %u <- ", n->id, n->pin); put_signal(&t, n->in[0]); break;
        case MSG_HW_KEYBOARD: put(&t, "KEYBOARD n%u <- ", n->id); put_signal(&t, n->in[0]); break;
        case MSG_HW_PWM:
            put(&t, "PWM n%u pin %u options %02X duty %u %u-%u <- ", n->id, n->pin, n->options,
                n->duty, n->duty_min, n->duty_max);
            put_signal(&t, n->in[0]);
            put(&t, " / %.4g freq %u %u-%u <- ", (double)n->in_full[0], n->freq, n->freq_min, n->freq_max);
            put_signal(&t, n->in[1]);
            put(&t, " / %.4g", (double)n->in_full[1]);
            break;
        default: put(&t, "? n%u", n->id); break;
        }
        put(&t, "\n");
    }
    return t.len;
}
