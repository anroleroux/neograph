#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "program.h"
#include "protocol.h"
#include "config/config_iterator.h"

bool program_logic(uint8_t type) {
    return type >= MSG_LOGIC_COMPARE && type <= MSG_LOGIC_LAST;
}

bool program_logic_floating(uint8_t type) {
    return type == MSG_LOGIC_SCALE || type == MSG_LOGIC_MIN || type == MSG_LOGIC_MAX || type == MSG_LOGIC_SINE;
}

bool program_ui_source(uint8_t type) {
    return type == MSG_UI_BUTTON || type == MSG_UI_SLIDER || type == MSG_UI_SLIDER_2D;
}

uint8_t program_signal(const hw_program *p, uint16_t id, uint8_t output) {
    for (uint8_t i = 0; i < p->signal_count; i++) {
        if (p->signals[i].id == id && p->signals[i].output == output) return i;
    }
    return PROG_NO_SIGNAL;
}

static float signal_value(const hw_program *p, uint8_t s) {
    return s == PROG_NO_SIGNAL ? 0 : p->signals[s].value;
}

// Whether a boolean signal is on: from 0.5 up, as an LED or GPO. One wired to
// nothing is off.
static bool signal_on(const hw_program *p, uint8_t s) {
    return signal_value(p, s) >= 0.5f;
}

void program_logic_start(const hw_program *p, const prog_node *n, prog_logic_state *st) {
    *st = (prog_logic_state){ 0 };
    if (n->type == MSG_LOGIC_PULSE || n->type == MSG_LOGIC_TOGGLE || n->type == MSG_LOGIC_COUNTER)
        st->in_was = signal_on(p, n->in[0]);
}

// A compare switches at its threshold, and back only past it by its hysteresis,
// so a noisy input near the threshold doesn't flicker it. An AND, OR, min or max
// reads only its wired inputs: with none, it is 0. A delay follows its input
// once the input has held its new state for the on or off delay. A pulse is on
// for its time on from each time its input goes on. A blink, while its input is
// on (or with none wired), starts on, then turns over after each time on or off.
// A toggle turns over each time its input goes on. A latch goes on when set and
// off when reset, and reset wins. A counter counts its count input going on, up to
// its preset, and is on (done) at its preset; reset sets it back to 0, and wins. A
// sine, while its input is on (or with none wired), is its offset plus its amplitude
// times the sine of how far it is through its period, starting at its phase; off, it
// rests where it starts, and starts again from there when it goes on.
float program_logic_step(const hw_program *p, const prog_node *n, float out, prog_logic_state *st, uint32_t dt_ms) {
    bool on = out >= 0.5f;
    switch (n->type) {
    case MSG_LOGIC_COMPARE: {
        float in = signal_value(p, n->in[0]);
        if (n->options == COMPARE_BELOW) return on ? in <= n->threshold + n->hysteresis : in < n->threshold;
        return on ? in >= n->threshold - n->hysteresis : in > n->threshold;
    }

    case MSG_LOGIC_AND:
    case MSG_LOGIC_OR: {
        bool any = false, all = true;
        uint8_t wired = 0;
        for (uint8_t i = 0; i < n->in_count; i++) {
            if (n->in[i] == PROG_NO_SIGNAL) continue;
            wired++;
            bool in = signal_on(p, n->in[i]);
            any |= in;
            all &= in;
        }
        return wired && (n->type == MSG_LOGIC_AND ? all : any);
    }

    case MSG_LOGIC_MIN:
    case MSG_LOGIC_MAX: {
        bool wired = false;
        float result = 0;
        for (uint8_t i = 0; i < n->in_count; i++) {
            if (n->in[i] == PROG_NO_SIGNAL) continue;
            float in = signal_value(p, n->in[i]);
            if (!wired || (n->type == MSG_LOGIC_MIN ? in < result : in > result)) result = in;
            wired = true;
        }
        return result;
    }

    case MSG_LOGIC_SCALE: {
        const float *k = n->scale;      // the input's from and to, the output's from and to
        float y = k[2] + (signal_value(p, n->in[0]) - k[0]) / (k[1] - k[0]) * (k[3] - k[2]);
        if (n->options & SCALE_CLAMP) {
            float lo = k[2] < k[3] ? k[2] : k[3], hi = k[2] < k[3] ? k[3] : k[2];
            y = y < lo ? lo : y > hi ? hi : y;
        }
        return y;
    }

    case MSG_LOGIC_NOT:
        return !signal_on(p, n->in[0]);

    case MSG_LOGIC_DELAY: {
        bool in = signal_on(p, n->in[0]);
        if (in == on) {
            st->held_ms = 0;
            return on;
        }
        st->held_ms += dt_ms;
        if (st->held_ms < (in ? n->on_ms : n->off_ms)) return on;
        st->held_ms = 0;
        return in;
    }

    case MSG_LOGIC_PULSE: {
        bool in = signal_on(p, n->in[0]), rose = in && !st->in_was;
        st->in_was = in;
        if (rose) {
            st->held_ms = 0;
            return 1;
        }
        if (!on) return 0;
        st->held_ms += dt_ms;
        return st->held_ms < n->on_ms;
    }

    case MSG_LOGIC_BLINK: {
        bool enabled = n->in[0] == PROG_NO_SIGNAL || signal_on(p, n->in[0]);
        if (!enabled) {
            st->in_was = false;
            st->held_ms = 0;
            return 0;
        }
        if (!st->in_was) {          // it starts blinking: on
            st->in_was = true;
            st->held_ms = 0;
            return 1;
        }
        st->held_ms += dt_ms;
        if (st->held_ms < (on ? n->on_ms : n->off_ms)) return on;
        st->held_ms = 0;
        return !on;
    }

    case MSG_LOGIC_TOGGLE: {
        bool in = signal_on(p, n->in[0]), rose = in && !st->in_was;
        st->in_was = in;
        return rose ? !on : on;
    }

    case MSG_LOGIC_LATCH:
        if (signal_on(p, n->in[1])) return 0;
        return on || signal_on(p, n->in[0]);

    case MSG_LOGIC_COUNTER: {
        bool in = signal_on(p, n->in[0]), rose = in && !st->in_was;
        st->in_was = in;
        if (signal_on(p, n->in[1])) st->count = 0;
        else if (rose && st->count < n->preset) st->count++;
        return st->count >= n->preset;
    }

    case MSG_LOGIC_SINE: {
        // on_ms: its period; scale[0] its amplitude, scale[1] its offset, scale[2] its
        // phase in degrees; held_ms how far through its period it is. Off, it rests where
        // it starts.
        if (n->in[0] != PROG_NO_SIGNAL && !signal_on(p, n->in[0])) {
            st->held_ms = 0;
            return n->scale[1] + n->scale[0] * sinf(n->scale[2] * 0.017453293f);
        }
        float y = n->scale[1] + n->scale[0] * sinf(6.2831853f * (float)st->held_ms / (float)n->on_ms
                                                   + n->scale[2] * 0.017453293f);
        st->held_ms = (st->held_ms + dt_ms) % n->on_ms;
        return y;
    }

    default:
        return 0;
    }
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
    *n = (prog_node){ .type = type, .id = id, .out = PROG_NO_SIGNAL, .out_count = PROG_NO_SIGNAL, .in_full = { 1, 1 },
                      .px_under = PROG_NO_NODE };
    memset(n->in, PROG_NO_SIGNAL, sizeof n->in);
    return n;
}

// The value of a source's signal that drives an output to its max, such as a
// PWM's: 1 for a slider (its values are 0 to 1) or a scale, 3.3 V for an ADC,
// CORE_TEMP_FULL °C for the core temperature, for a min or max that of what it
// picks from, for a counter's count its preset, and for an encoder's position its
// steps.
static float signal_full(const hw_program *p, uint8_t s) {
    if (s == PROG_NO_SIGNAL) return 1;
    switch (p->signals[s].type) {
    case MSG_HW_ADC: return PROG_ADC_VOLTS;
    case MSG_HW_CORE: return CORE_TEMP_FULL;
    case MSG_HW_CLOCK: return p->signals[s].output == 0 ? CLOCK_HOURS : CLOCK_MINUTES;   // 24 h; 60 min or s
    case MSG_HW_DS18B20: return p->signals[s].output == 0 ? DS18B20_FULL : 1;  // °C, or found: 0 or 1
    case MSG_HW_ENCODER:
        // Its position: its steps; pressed: 0 or 1.
        for (uint8_t i = 0; i < p->node_count; i++) {
            if (p->nodes[i].type == MSG_HW_ENCODER && p->nodes[i].out == s) return (float)p->nodes[i].enc_steps;
        }
        return 1;
    case MSG_LOGIC_COUNTER:
        for (uint8_t i = 0; i < p->node_count; i++) {
            if (p->nodes[i].out_count == s) return (float)p->nodes[i].preset;
        }
        return 1;           // its done output: 0 or 1
    case MSG_LOGIC_MIN:
    case MSG_LOGIC_MAX:
        // The full scale of what it picks from: of its first wired input.
        for (uint8_t i = 0; i < p->node_count; i++) {
            const prog_node *n = &p->nodes[i];
            if (n->out != s) continue;
            for (uint8_t k = 0; k < n->in_count; k++) {
                if (n->in[k] != PROG_NO_SIGNAL) return signal_full(p, n->in[k]);
            }
        }
        return 1;
    default: return 1;      // a slider, a 2D slider, a scale: 0 to 1
    }
}

// The config program_compile is compiling, where resolve_ref looks blocks up.
static const uint8_t *compiling;
static size_t compiling_len;

// What a reference stands for: a reference to an output of a block (MSG_SUB) stands
// for the reference in the block's table for that output, which may itself name a
// block's output, up to SUB_PORTS_MAX blocks deep. A feedback wire on the way makes
// it a feedback one. Not connected if it names an output a block doesn't have, or
// goes deeper.
static msg_ref resolve_ref(msg_ref ref) {
    for (int depth = 0; depth <= SUB_PORTS_MAX; depth++) {
        config_element_iterator itr;
        config_element_iterator_init(&itr, compiling, compiling_len);
        bool block = false;
        while (!block && itr.next(&itr)) block = itr.type == MSG_SUB && itr.id == ref.id && ref.id;
        if (!block) return ref;
        if (!sub_payload_valid(itr.pay, itr.len, itr.flags) || ref.output >= itr.pay[4]) break;
        bool feedback = ref.feedback;
        ref = msg_ref_at(itr.pay + 5 + 3 * (itr.pay[3] + ref.output), itr.flags);
        ref.feedback |= feedback;
    }
    return (msg_ref){ 0, 0, 0 };
}

static uint8_t ref_signal(const hw_program *p, msg_ref ref) {
    ref = resolve_ref(ref);
    return ref.id ? program_signal(p, ref.id, ref.output) : PROG_NO_SIGNAL;
}

// Whether node n reads a signal that a logic node in nodes[from, to) writes, other
// than through a feedback wire.
static bool reads_logic(const hw_program *p, const prog_node *n, uint8_t from, uint8_t to) {
    for (uint8_t i = 0; i < n->in_count; i++) {
        if (n->in[i] == PROG_NO_SIGNAL || n->feedback & 1 << i) continue;
        for (uint8_t j = from; j < to; j++) {
            if (n->in[i] == p->nodes[j].out || n->in[i] == p->nodes[j].out_count) return true;
        }
    }
    return false;
}

// Put the logic nodes, nodes[first, node_count), in the order core 1 runs them:
// each after the logic it reads, so a scan carries a change through a chain of
// them. Otherwise they keep the config's order. A feedback wire doesn't count:
// the logic it comes from runs after the logic it goes into, which so reads it as
// the last scan left it. In a loop with no feedback wire, which the UI doesn't
// draw, the first of its nodes left comes first, and reads the others' signals
// as the last scan left them.
static void order_logic(hw_program *p, uint8_t first) {
    for (uint8_t placed = first; placed < p->node_count; placed++) {
        uint8_t pick = placed;
        for (uint8_t i = placed; i < p->node_count; i++) {
            if (!reads_logic(p, &p->nodes[i], placed, p->node_count)) { pick = i; break; }
        }
        prog_node n = p->nodes[pick];
        memmove(&p->nodes[placed + 1], &p->nodes[placed], (pick - placed) * sizeof n);
        p->nodes[placed] = n;
    }
}

// Whether two NeoPixel nodes' LEDs are the same: their layout, size and wiring.
static bool same_strip(const neopixel_payload *a, const neopixel_payload *b) {
    return a->layout == b->layout && a->options == b->options && a->size_len == b->size_len
        && !memcmp(a->size, b->size, a->size_len);
}

// Join the NeoPixel nodes into chains: each draws over the node on its data line in,
// which then feeds it rather than driving its pin. A data line in is left unjoined if
// it names no NeoPixel node, one with other LEDs (layout, size or wiring), one that
// already feeds another (in program order, the first wins), or one that, through its
// own data line in, would come back to it.
static void link_neopixels(hw_program *p) {
    for (uint8_t i = 0; i < p->node_count; i++) {
        prog_node *n = &p->nodes[i];
        if (n->type != MSG_HW_NEOPIXEL || !n->px.under.id) continue;
        uint8_t j = 0;
        while (j < p->node_count && !(p->nodes[j].type == MSG_HW_NEOPIXEL && p->nodes[j].id == n->px.under.id)) j++;
        if (j == p->node_count || j == i || p->nodes[j].px_feeds || !same_strip(&n->px, &p->nodes[j].px)) continue;
        bool loop = false;
        for (uint8_t k = j, steps = 0; k != PROG_NO_NODE && steps <= p->node_count; k = p->nodes[k].px_under, steps++)
            if (k == i) loop = true;
        if (loop) continue;
        n->px_under = j;
        p->nodes[j].px_feeds = true;
    }
}

void program_compile(const uint8_t *payload, size_t len, hw_program *p) {
    p->signal_count = 0;
    p->node_count = 0;
    compiling = payload;
    compiling_len = len;

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

        case MSG_HW_CLOCK: {
            // Three signals, in a row: its hour, minute and second. Its settings are core 0's
            // (timesync.c).
            uint8_t options;
            int16_t zone;
            if (!clock_payload_read(itr.pay, itr.len, &options, &zone)) break;
            if ((s = add_signal(p, itr.id, 0, itr.type, 0)) == PROG_NO_SIGNAL) break;
            if (add_signal(p, itr.id, 1, itr.type, 0) != s + 1 || add_signal(p, itr.id, 2, itr.type, 0) != s + 2) break;
            if ((n = add_node(p, itr.type, itr.id))) n->out = s;
            break;
        }

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

        case MSG_HW_DS18B20: {
            // Two signals: its temperature, and whether the sensor answers.
            if (itr.len != 2 || !pin_usable(itr.pay[0]) || (itr.pay[1] & ~(DS18B20_RES_MASK | DS18B20_PULLUP))) break;
            uint8_t found;
            if ((s = add_signal(p, itr.id, 0, itr.type, 0)) == PROG_NO_SIGNAL) break;
            if ((found = add_signal(p, itr.id, 1, itr.type, 0)) == PROG_NO_SIGNAL) break;
            if ((n = add_node(p, itr.type, itr.id))) {
                n->out = s;
                n->out_count = found;
                n->pin = itr.pay[0];
                n->options = itr.pay[1];
            }
            break;
        }

        case MSG_HW_ENCODER: {
            // Two signals: its position, and whether its switch is pressed.
            if (!encoder_payload_valid(itr.pay, itr.len)) break;
            uint8_t pressed;
            if ((s = add_signal(p, itr.id, 0, itr.type, 0)) == PROG_NO_SIGNAL) break;
            if ((pressed = add_signal(p, itr.id, 1, itr.type, 0)) == PROG_NO_SIGNAL) break;
            if ((n = add_node(p, itr.type, itr.id))) {
                n->out = s;
                n->out_count = pressed;
                memcpy(n->enc_pins, itr.pay, sizeof n->enc_pins);
                n->pin = itr.pay[0];
                n->options = itr.pay[3];
                n->enc_steps = (uint16_t)(itr.pay[4] << 8 | itr.pay[5]);
            }
            break;
        }

        default:
            // A logic element's signal, for the elements that read it; its node
            // comes after the inputs.
            if (program_logic(itr.type) && logic_payload_valid(itr.type, itr.pay, itr.len, itr.flags)) {
                add_signal(p, itr.id, 0, itr.type, 0);
                if (itr.type == MSG_LOGIC_COUNTER) add_signal(p, itr.id, 1, itr.type, 0);   // its count
            }
            break;
        }
    }

    // The logic, after the inputs: a node for each logic element whose signal
    // the program has, then in the order it runs.
    uint8_t first_logic = p->node_count;
    itr.reset(&itr);
    while (itr.next(&itr)) {
        if (!program_logic(itr.type) || !logic_payload_valid(itr.type, itr.pay, itr.len, itr.flags)) continue;
        uint8_t s = program_signal(p, itr.id, 0);
        if (s == PROG_NO_SIGNAL || p->signals[s].type != itr.type) continue;
        prog_node *n = add_node(p, itr.type, itr.id);
        if (!n) break;
        n->out = s;

        uint8_t r = msg_ref_len(itr.flags);
        n->in_count = itr.type == MSG_LOGIC_AND || itr.type == MSG_LOGIC_OR || itr.type == MSG_LOGIC_MIN
            || itr.type == MSG_LOGIC_MAX ? itr.len / r
            : itr.type == MSG_LOGIC_LATCH || itr.type == MSG_LOGIC_COUNTER ? 2 : 1;
        for (uint8_t i = 0; i < n->in_count; i++) {
            msg_ref ref = resolve_ref(msg_ref_at(itr.pay + i * r, itr.flags));
            n->in[i] = ref_signal(p, ref);
            if (ref.feedback) n->feedback |= 1 << i;
        }
        if (itr.type == MSG_LOGIC_COMPARE) {
            n->options = itr.pay[r];
            n->threshold = msg_float_at(itr.pay + r + 1);
            n->hysteresis = msg_float_at(itr.pay + r + 5);
        } else if (itr.type == MSG_LOGIC_DELAY || itr.type == MSG_LOGIC_BLINK) {
            n->on_ms = msg_u32_at(itr.pay + r);
            n->off_ms = msg_u32_at(itr.pay + r + 4);
        } else if (itr.type == MSG_LOGIC_PULSE) {
            n->on_ms = msg_u32_at(itr.pay + r);
        } else if (itr.type == MSG_LOGIC_SINE) {
            n->on_ms = msg_u32_at(itr.pay + r);
            n->scale[0] = msg_float_at(itr.pay + r + 4);
            n->scale[1] = msg_float_at(itr.pay + r + 8);
            n->scale[2] = itr.len == r + 16 ? msg_float_at(itr.pay + r + 12) : 0;
        } else if (itr.type == MSG_LOGIC_SCALE) {
            n->options = itr.pay[r];
            for (int i = 0; i < 4; i++) n->scale[i] = msg_float_at(itr.pay + r + 1 + 4 * i);
        } else if (itr.type == MSG_LOGIC_COUNTER) {
            n->out_count = program_signal(p, itr.id, 1);
            n->preset = msg_u32_at(itr.pay + 2 * r);
        }
    }
    order_logic(p, first_logic);

    // The outputs, after every signal they may read.
    itr.reset(&itr);
    while (itr.next(&itr)) {
        prog_node *n;
        uint8_t r = msg_ref_len(itr.flags);
        switch (itr.type) {
        case MSG_HW_LED:
            if (itr.len != r) break;
            if ((n = add_node(p, itr.type, itr.id))) {
                n->in_count = 1;
                n->in[0] = ref_signal(p, msg_ref_at(itr.pay, itr.flags));
            }
            break;

        case MSG_HW_GPO:
            if (itr.len != r + 1) break;
            if ((n = add_node(p, itr.type, itr.id))) {
                n->in_count = 1;
                n->in[0] = ref_signal(p, msg_ref_at(itr.pay, itr.flags));
                n->pin = itr.pay[r];
            }
            break;

        case MSG_HW_KEYBOARD:
            // Core 0 keeps the keystroke; core 1 says when its input goes on.
            if (!keyboard_payload_valid(itr.pay, itr.len, itr.flags)) break;
            if ((n = add_node(p, itr.type, itr.id))) {
                n->in_count = 1;
                n->in[0] = ref_signal(p, msg_ref_at(itr.pay, itr.flags));
            }
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
            n->in_count = 2;
            n->in[0] = ref_signal(p, pwm.duty_source);
            n->in[1] = ref_signal(p, pwm.freq_source);
            for (int i = 0; i < 2; i++) n->in_full[i] = signal_full(p, n->in[i]);
            break;
        }

        case MSG_HW_SERVO: {
            servo_payload sv;
            if (!servo_payload_read(itr.pay, itr.len, itr.flags, &sv)) break;
            if (!(n = add_node(p, itr.type, itr.id))) break;
            n->in_count = 1;
            n->in[0] = ref_signal(p, sv.source);
            n->in_full[0] = signal_full(p, n->in[0]);
            n->pin = sv.pin;
            n->angle_min = sv.angle_min;
            n->angle_max = sv.angle_max;
            n->pulse_min = sv.pulse_min;
            n->pulse_max = sv.pulse_max;
            n->slew = sv.speed;
            break;
        }

        case MSG_HW_NEOPIXEL: {
            neopixel_payload px;
            if (!neopixel_payload_read(itr.pay, itr.len, itr.flags, &px)) break;
            if (!(n = add_node(p, itr.type, itr.id))) break;
            n->in_count = 2;
            n->in[0] = ref_signal(p, px.source);
            n->in[1] = ref_signal(p, px.enable);
            n->in_full[0] = signal_full(p, n->in[0]);
            n->pin = px.pin;
            n->px = px;
            break;
        }

        case MSG_HW_STEPPER: {
            stepper_payload sp;
            if (!stepper_payload_read(itr.pay, itr.len, itr.flags, &sp)) break;
            if (!(n = add_node(p, itr.type, itr.id))) break;
            n->in_count = 1;
            n->in[0] = ref_signal(p, sp.source);
            memcpy(n->coil_pins, sp.pins, sizeof n->coil_pins);
            n->pin = sp.pins[0];
            n->options = sp.options;
            n->steps_rev = sp.steps_rev;
            n->turn_scale = sp.scale;
            n->rpm = sp.rpm;
            n->accel = sp.accel;
            break;
        }

        default:
            break;
        }
    }
    link_neopixels(p);
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
    case MSG_HW_DS18B20: return "ds18b20";
    case MSG_HW_ENCODER: return "encoder";
    case MSG_HW_CLOCK: return "clock";
    case MSG_LOGIC_COMPARE: return "compare";
    case MSG_LOGIC_AND: return "and";
    case MSG_LOGIC_OR: return "or";
    case MSG_LOGIC_NOT: return "not";
    case MSG_LOGIC_DELAY: return "delay";
    case MSG_LOGIC_SCALE: return "scale";
    case MSG_LOGIC_MIN: return "min";
    case MSG_LOGIC_MAX: return "max";
    case MSG_LOGIC_PULSE: return "pulse";
    case MSG_LOGIC_BLINK: return "blink";
    case MSG_LOGIC_TOGGLE: return "toggle";
    case MSG_LOGIC_LATCH: return "latch";
    case MSG_LOGIC_COUNTER: return "counter";
    case MSG_LOGIC_SINE: return "sine";
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

// A node's k-th input: its signal, marked ' if it comes through a feedback wire
// (it is read as the last scan left it).
static void put_input(text *t, const prog_node *n, uint8_t k) {
    put_signal(t, n->in[k]);
    if (n->feedback & 1 << k) put(t, "'");
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
        case MSG_HW_CLOCK:
            put(&t, "CLOCK n%u -> ", n->id);
            put_signal(&t, n->out);
            put(&t, " ");
            put_signal(&t, (uint8_t)(n->out + 1));
            put(&t, " ");
            put_signal(&t, (uint8_t)(n->out + 2));
            break;
        case MSG_HW_ADC: put(&t, "ADC n%u channel %u -> ", n->id, n->pin); put_signal(&t, n->out); break;
        case MSG_HW_GPI: put(&t, "GPI n%u pin %u options %02X -> ", n->id, n->pin, n->options); put_signal(&t, n->out); break;
        case MSG_HW_DS18B20:
            put(&t, "DS18B20 n%u pin %u %u-bit%s -> ", n->id, n->pin, 9 + (n->options & DS18B20_RES_MASK),
                n->options & DS18B20_PULLUP ? " pull-up" : "");
            put_signal(&t, n->out);
            put(&t, " found ");
            put_signal(&t, n->out_count);
            break;
        case MSG_HW_ENCODER: {
            static const char *const detents[] = { "4", "2", "1" };
            put(&t, "ENCODER n%u pins %u %u ", n->id, n->enc_pins[0], n->enc_pins[1]);
            if (n->enc_pins[2] == ENCODER_NO_BUTTON) put(&t, "no button");
            else put(&t, "button %u", n->enc_pins[2]);
            put(&t, " %s/click steps %u%s%s -> ", detents[n->options & ENCODER_DETENT_MASK], n->enc_steps,
                n->options & ENCODER_REVERSE ? " reverse" : "", n->options & ENCODER_WRAP ? " wrap" : "");
            put_signal(&t, n->out);
            put(&t, " pressed ");
            put_signal(&t, n->out_count);
            break;
        }
        case MSG_LOGIC_COMPARE:
            put(&t, "COMPARE n%u %s %.4g hysteresis %.4g <- ", n->id, n->options == COMPARE_BELOW ? "below" : "above",
                (double)n->threshold, (double)n->hysteresis);
            put_input(&t, n, 0);
            put(&t, " -> ");
            put_signal(&t, n->out);
            break;
        case MSG_LOGIC_AND:
        case MSG_LOGIC_OR:
        case MSG_LOGIC_NOT:
        case MSG_LOGIC_MIN:
        case MSG_LOGIC_MAX:
        case MSG_LOGIC_TOGGLE:
            put(&t, "%s n%u <-", n->type == MSG_LOGIC_AND ? "AND" : n->type == MSG_LOGIC_OR ? "OR"
                : n->type == MSG_LOGIC_NOT ? "NOT" : n->type == MSG_LOGIC_MIN ? "MIN"
                : n->type == MSG_LOGIC_MAX ? "MAX" : "TOGGLE", n->id);
            for (uint8_t k = 0; k < n->in_count; k++) { put(&t, " "); put_input(&t, n, k); }
            put(&t, " -> ");
            put_signal(&t, n->out);
            break;
        case MSG_LOGIC_SCALE:
            put(&t, "SCALE n%u %.4g-%.4g to %.4g-%.4g%s <- ", n->id, (double)n->scale[0], (double)n->scale[1],
                (double)n->scale[2], (double)n->scale[3], n->options & SCALE_CLAMP ? " clamp" : "");
            put_input(&t, n, 0);
            put(&t, " -> ");
            put_signal(&t, n->out);
            break;
        case MSG_LOGIC_PULSE:
        case MSG_LOGIC_BLINK:
            put(&t, "%s n%u on %lu", n->type == MSG_LOGIC_PULSE ? "PULSE" : "BLINK", n->id, (unsigned long)n->on_ms);
            if (n->type == MSG_LOGIC_BLINK) put(&t, " off %lu", (unsigned long)n->off_ms);
            put(&t, " <- ");
            put_input(&t, n, 0);
            put(&t, " -> ");
            put_signal(&t, n->out);
            break;
        case MSG_LOGIC_LATCH:
            put(&t, "LATCH n%u set ", n->id);
            put_input(&t, n, 0);
            put(&t, " reset ");
            put_input(&t, n, 1);
            put(&t, " -> ");
            put_signal(&t, n->out);
            break;
        case MSG_LOGIC_COUNTER:
            put(&t, "COUNTER n%u preset %lu count ", n->id, (unsigned long)n->preset);
            put_input(&t, n, 0);
            put(&t, " reset ");
            put_input(&t, n, 1);
            put(&t, " -> ");
            put_signal(&t, n->out);
            put(&t, " ");
            put_signal(&t, n->out_count);
            break;
        case MSG_LOGIC_SINE:
            put(&t, "SINE n%u period %lu amplitude %.4g offset %.4g phase %.4g <- ", n->id, (unsigned long)n->on_ms,
                (double)n->scale[0], (double)n->scale[1], (double)n->scale[2]);
            put_input(&t, n, 0);
            put(&t, " -> ");
            put_signal(&t, n->out);
            break;
        case MSG_LOGIC_DELAY:
            put(&t, "DELAY n%u on %lu off %lu <- ", n->id, (unsigned long)n->on_ms, (unsigned long)n->off_ms);
            put_input(&t, n, 0);
            put(&t, " -> ");
            put_signal(&t, n->out);
            break;
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
        case MSG_HW_STEPPER: {
            static const char *const drives[] = { "wave", "full", "half" };
            put(&t, "STEPPER n%u pins %u %u %u %u %s %u steps %s %.4g %s max %.4g rpm accel %.4g%s%s <- ", n->id,
                n->coil_pins[0], n->coil_pins[1], n->coil_pins[2], n->coil_pins[3],
                drives[n->options & STEPPER_DRIVE_MASK], n->steps_rev,
                n->options & STEPPER_SPEED ? "speed" : "position", (double)n->turn_scale,
                n->options & STEPPER_SPEED ? "rpm/unit" : "rev/unit", (double)n->rpm, (double)n->accel,
                n->options & STEPPER_REVERSE ? " reverse" : "", n->options & STEPPER_RELEASE ? " release" : "");
            put_signal(&t, n->in[0]);
            break;
        }
        case MSG_HW_SERVO:
            put(&t, "SERVO n%u pin %u angle %u-%u pulse %u-%u us speed %u <- ", n->id, n->pin,
                n->angle_min, n->angle_max, n->pulse_min, n->pulse_max, n->slew);
            put_signal(&t, n->in[0]);
            put(&t, " / %.4g", (double)n->in_full[0]);
            break;
        case MSG_HW_NEOPIXEL: {
            const neopixel_payload *px = &n->px;
            put(&t, "NEOPIXEL n%u pin %u ", n->id, n->pin);
            if (px->layout == NEOPIXEL_ARRAY) {
                put(&t, "array %ux%u%s", px->size[0], px->size[1], px->options & NEOPIXEL_ZIGZAG ? " zigzag" : "");
            } else if (px->layout == NEOPIXEL_CIRCLE) {
                put(&t, "circle");
                for (uint8_t k = 0; k < px->size_len; k++) put(&t, "%s%u", k ? "+" : " ", px->size[k]);
            } else {
                put(&t, "doughnut %u", px->size[0]);
            }
            put(&t, " brightness %u ", px->brightness);
            if (px->graphic == NEOPIXEL_GFX_POINT || px->graphic == NEOPIXEL_GFX_PATCH) {
                put(&t, "%s %u,%u-%u,%u", px->graphic == NEOPIXEL_GFX_POINT ? "point" : "patch",
                    px->from[0], px->from[1], px->to[0], px->to[1]);
            } else {
                put(&t, "bar ");
                if (px->layout == NEOPIXEL_ARRAY) put(&t, "%s %u", px->along == NEOPIXEL_BAR_ROW ? "row" : "column", px->position);
                else if (px->layout == NEOPIXEL_CIRCLE && px->along == NEOPIXEL_BAR_COLUMN) put(&t, "outwards");
                else put(&t, "%s %u", px->layout == NEOPIXEL_CIRCLE ? "ring" : "from", px->position);
            }
            put(&t, "%s colour %02X%02X%02X", px->gfx_options & NEOPIXEL_GFX_REVERSE ? " reverse" : "",
                px->colour[0][0], px->colour[0][1], px->colour[0][2]);
            if (px->gfx_options & NEOPIXEL_GFX_COLOUR_INPUT)
                put(&t, "-%02X%02X%02X", px->colour[1][0], px->colour[1][1], px->colour[1][2]);
            put(&t, " <- ");
            put_signal(&t, n->in[0]);
            put(&t, " / %.4g on <- ", (double)n->in_full[0]);
            put_signal(&t, n->in[1]);
            if (n->px_under != PROG_NO_NODE) put(&t, " over n%u", p->nodes[n->px_under].id);
            if (n->px_feeds) put(&t, " feeds");
            break;
        }
        default: put(&t, "? n%u", n->id); break;
        }
        put(&t, "\n");
    }
    return t.len;
}
