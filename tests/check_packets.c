/*
 * Check packets captured from the UI (tests/ui_packets.cjs) with the firmware's
 * own parsing code, so a UI change that the device would reject or misread fails
 * CI instead of failing on hardware.
 *
 *   ./check_packets ui/dist/packets.txt     (one packet per line, as hex)
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "config/config_iterator.h"

#define SECRETS_REGION 256      /* SECURE_FLASH_SIZE in config.h */

static int failures = 0;
static int line_no = 0;

static void fail(const char *what) {
    printf("packet %d: %s\n", line_no, what);
    failures++;
}

/* The config the device would hold: the last config the UI sent. */
static uint8_t stored[PKT_CONFIG_MAX];
static size_t stored_len = 0;

/* The first-boot secrets: network 0's password. */
static const uint8_t stored_secrets[] = {
    PKT_CONFIG, 0x00, 0x0E,
    MSG_NETWORK_FIELD, 0x00, 0x00, 0x00, 0x09, FIELD_NETWORK_PASSWORD, '1', '2', '3', '4', '5', '6', '7', '8',
};

static bool is_control(uint8_t type) { return type <= 0x3F; }

/* The type of the control element with an ID in a config; 0 if there is none. */
static uint8_t control_type(const uint8_t *p, size_t len, uint16_t id) {
    config_element_iterator it;
    config_element_iterator_init(&it, p + 3, len - 3);
    while (it.next(&it))
        if (is_control(it.type) && it.id == id) return it.type;
    return 0;
}

/* The outputs a source of a type has: a 2D slider's X and Y, one for the others. */
static uint8_t outputs_of(uint8_t type) { return type == MSG_UI_SLIDER_2D ? 2 : 1; }

/* Whether a message holds source references, which the UI writes with their outputs. */
static bool has_refs(uint8_t type) {
    return type == MSG_HW_LED || type == MSG_HW_GPO || type == MSG_HW_PWM || type == MSG_HW_KEYBOARD
        || type == MSG_UI_GRAPH || type == MSG_UI_INDICATOR;
}

/* A source reference must name an output of a source the output takes (PROTOCOL.md,
   "Wire types"): a button or GPIO input for an LED, GPO, keyboard or light, a slider, 2D slider or an ADC
   for a PWM, and those or the core temperature for a plot. `types` lists the types it
   may be, ending in 0. */
static const uint8_t BOOLEAN[] = { MSG_UI_BUTTON, MSG_HW_GPI, 0 };
static const uint8_t FLOATING_HW[] = { MSG_UI_SLIDER, MSG_UI_SLIDER_2D, MSG_HW_ADC, 0 };
static const uint8_t FLOATING[] = { MSG_UI_SLIDER, MSG_UI_SLIDER_2D, MSG_HW_ADC, MSG_HW_CORE, 0 };

static void check_source(const uint8_t *p, size_t len, msg_ref src, const uint8_t *types, const char *what) {
    if (src.id == 0) {
        if (src.output != 0) fail("an empty source reference has an output");
        return;
    }
    uint8_t type = control_type(p, len, src.id);
    if (type == 0) { fail("a source reference names no element in the config"); return; }
    if (src.output >= outputs_of(type)) fail("a source reference names an output its source does not have");
    for (; *types; types++) if (type == *types) return;
    fail(what);
}

static void check_config(const uint8_t *p, size_t len) {
    if (len > PKT_CONFIG_MAX) fail("config is bigger than PKT_CONFIG_MAX");

    config_element_iterator it, ctl;
    config_element_iterator_init(&it, p + 3, len - 3);
    bool device = false;
    while (it.next(&it)) {
        /* The UI writes every source reference with its output. */
        uint8_t r = msg_ref_len(it.flags);
        if (has_refs(it.type) && !(it.flags & MSG_FLAG_OUTPUT)) fail("source references are written without their outputs");
        switch (it.type) {
        case MSG_DEVICE:
            device = true;
            break;
        case MSG_UI_SLIDER:
            if (it.len != 1) fail("slider message is not 1 byte");
            else if (it.pay[0] & ~SLIDER_VERTICAL) fail("slider orientation has bits other than SLIDER_VERTICAL");
            break;
        case MSG_UI_INDICATOR:
            if (it.len != r) fail("light message is not one source reference");
            else check_source(p, len, msg_ref_at(it.pay, it.flags), BOOLEAN, "a light is wired from something other than a button or a GPIO input");
            break;
        case MSG_UI_SLIDER_2D:
            if (it.len != 4) fail("2D slider message is not its options, return speed and home");
            else if (it.pay[0] & ~(SLIDER_RETURN | SLIDER_EASE_MASK)) fail("2D slider options have unknown bits");
            else if ((it.pay[0] & SLIDER_EASE_MASK) == SLIDER_EASE_MASK) fail("2D slider return has no such shape");
            else if (it.pay[1] == 0) fail("2D slider return speed is 0");
            break;
        case MSG_UI_GRAPH:
            if (it.len < r + 1 || it.len > r + 1 + 16) fail("plot message is not a source and window plus a unit of up to 16");
            else if (it.pay[r] == 0) fail("plot window is 0 seconds");
            else check_source(p, len, msg_ref_at(it.pay, it.flags), FLOATING,
                              "a plot is wired from something other than a slider, an ADC or the core temperature");
            break;
        case MSG_UI_BUTTON:
            /* The device ignores it; the UI restores the options byte. */
            if (it.len > 1) fail("button message is longer than 1 byte");
            else if (it.len == 1 && (it.pay[0] & ~BUTTON_MOMENTARY)) fail("button options set an unknown bit");
            break;
        case MSG_HW_CORE:
            if (it.len != 0) fail("core temperature message is not empty");
            break;
        case MSG_HW_GPI:
            if (it.len != 2) fail("GPIO input message is not 2 bytes; the device would skip it");
            else if (!pin_usable(it.pay[0])) fail("GPIO input pin is not in PIN_USABLE_MASK");
            else if (it.pay[1] & ~(GPI_PULL_MASK | GPI_INVERT)) fail("GPIO input options have unknown bits");
            else if ((it.pay[1] & GPI_PULL_MASK) == GPI_PULL_MASK) fail("GPIO input has no such pull");
            break;
        case MSG_HW_ADC:
            if (it.len != 1) fail("ADC message is not 1 byte; the device would skip it");
            else if (it.pay[0] >= ADC_CHANNELS) fail("ADC channel is not below ADC_CHANNELS");
            break;
        /* unmarshal_controls only sets up outputs with exactly these lengths */
        case MSG_HW_LED:
            if (it.len != r) fail("LED message is not one source reference; the device would skip it");
            else check_source(p, len, msg_ref_at(it.pay, it.flags), BOOLEAN, "an LED is wired from something other than a button or a GPIO input");
            break;
        case MSG_HW_GPO:
            if (it.len != r + 1) fail("GPO message is not a source and a pin; the device would skip it");
            else if (!pin_usable(it.pay[r])) fail("GPO pin is not in PIN_USABLE_MASK");
            else check_source(p, len, msg_ref_at(it.pay, it.flags), BOOLEAN, "a GPO is wired from something other than a button or a GPIO input");
            break;
        case MSG_HW_KEYBOARD:
            if (!keyboard_payload_valid(it.pay, it.len, it.flags)) fail("keyboard payload is not one the device types; it would skip it");
            else check_source(p, len, msg_ref_at(it.pay, it.flags), BOOLEAN, "a keyboard is wired from something other than a button or a GPIO input");
            break;
        case MSG_HW_PWM: {
            pwm_payload pwm;
            if (!pwm_payload_read(it.pay, it.len, it.flags, &pwm)) fail("PWM message is not a PWM's length; the device would skip it");
            else if (!pin_usable(pwm.pin)) fail("PWM pin is not in PIN_USABLE_MASK");
            else if (pwm.freq < PWM_FREQ_MIN || pwm.freq_min < PWM_FREQ_MIN || pwm.freq_max < PWM_FREQ_MIN)
                fail("a PWM frequency is under PWM_FREQ_MIN");
            else if (pwm.options & ~(PWM_DUTY_INPUT | PWM_FREQ_INPUT | PWM_FREQ_LOG)) fail("PWM options have unknown bits");
            else if (pwm.duty > PWM_DUTY_MAX || pwm.duty_min > PWM_DUTY_MAX || pwm.duty_max > PWM_DUTY_MAX)
                fail("a PWM duty cycle is over PWM_DUTY_MAX");
            else if (pwm.duty_source.id && !(pwm.options & PWM_DUTY_INPUT))
                fail("a PWM has a duty source but a constant duty");
            else if (pwm.freq_source.id && !(pwm.options & PWM_FREQ_INPUT))
                fail("a PWM has a frequency source but a constant frequency");
            else {
                check_source(p, len, pwm.duty_source, FLOATING_HW, "a PWM's duty is wired from something other than a slider or an ADC");
                check_source(p, len, pwm.freq_source, FLOATING_HW, "a PWM's frequency is wired from something other than a slider or an ADC");
            }
            break;
        }
        case MSG_STRING:
        case MSG_LAYOUT:
        case MSG_WIRE: {
            /* A wire into a PWM ends with its source reference, written with its output. */
            if (it.type == MSG_STRING ? it.len == 0 : it.type == MSG_WIRE ? (it.len != 4 && it.len != 4 + 3) : it.len != 4)
                fail("label, position or wire has the wrong length");
            else if (it.type == MSG_WIRE && it.len > 4 && !(it.flags & MSG_FLAG_OUTPUT))
                fail("a wire's source reference is written without its output");
            bool found = false;
            config_element_iterator_init(&ctl, p + 3, len - 3);
            while (ctl.next(&ctl))
                if (is_control(ctl.type) && ctl.id == it.id) found = true;
            if (!found) fail("label, position or wire for a node that is not in the config");
            break;
        }
        default:
            break;
        }
    }
    if (!device) fail("config has no device message; the device would lose its networking");

    memcpy(stored, p, len);
    stored_len = len;
}

static void check_command(const uint8_t *p, size_t len) {
    config_element_iterator it;
    config_element_iterator_init(&it, p + 3, len - 3);
    while (it.next(&it)) {
        if ((it.type & (MSG_HW_BIT | MSG_INPUT_BIT)) != MSG_INPUT_BIT) fail("command from an element that is not a UI input");
        /* handle_packet sends one action word per payload byte: a value for each output */
        if (it.len != outputs_of(it.type)) fail("command payload is not one byte per output of its element");
        if (it.len > FIFO_OUTPUTS_MAX) fail("command has more values than an action word can name outputs");
    }
}

static void check_update(const uint8_t *p, size_t len, bool secret) {
    config_element_iterator it;
    config_element_iterator_init(&it, p + 3, len - 3);
    int n = 0;
    while (it.next(&it)) {
        n++;
        if (it.type != MSG_NETWORK_FIELD || it.len == 0) continue;
        uint8_t field = it.pay[0];
        if (field == FIELD_NETWORK_PASSWORD && !secret) fail("a password sent to the config store, which the UI reads back");
        if (field == FIELD_NETWORK_AP_ADDRESS || field == FIELD_NETWORK_STATIC_ADDRESS || field == FIELD_NETWORK_GATEWAY) {
            static const uint8_t unset[4];
            if (it.len != 5) fail("address field is not 4 bytes; the device would ignore it");
            else if (memcmp(it.pay + 1, unset, 4) == 0) continue;
            else if (field == FIELD_NETWORK_AP_ADDRESS ? !net_ap_address_usable(it.pay + 1) : !net_address_usable(it.pay + 1))
                fail("address field holds an address the device would not use");
        }
    }
    if (n != 1) fail("field update does not hold exactly one message");

    const uint8_t *target = secret ? stored_secrets : stored;
    size_t target_len = secret ? sizeof stored_secrets : stored_len;
    if (!secret && stored_len == 0) {
        fail("field update before any config was sent");
        return;
    }
    /* what update_config / update_secrets would write: the config store also adds a
       field the network doesn't have yet */
    uint8_t out[PKT_CONFIG_MAX];
    size_t region = secret ? SECRETS_REGION : PKT_CONFIG_MAX;
    size_t updated = secret ? config_replace_field(target, target_len, p + 3, len - 3, out, region)
                            : config_set_field(target, target_len, p + 3, len - 3, out, region);
    if (updated == 0) fail("field update matches no stored field or element; the device would ignore it");
    else if (!secret) {
        /* later updates and saves build on it */
        memcpy(stored, out, updated);
        stored_len = updated;
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s packets.txt\n", argv[0]);
        return 2;
    }
    FILE *f = fopen(argv[1], "r");
    if (!f) {
        perror(argv[1]);
        return 2;
    }

    static char line[2 * PKT_CONFIG_MAX + 64];
    int packets = 0;
    while (fgets(line, sizeof line, f)) {
        line_no++;
        static uint8_t p[PKT_CONFIG_MAX + 32];
        size_t len = 0;
        for (char *c = line; c[0] && c[1] && c[0] != '\n' && len < sizeof p; c += 2)
            sscanf(c, "%2hhx", &p[len++]);
        if (len == 0) continue;
        packets++;

        /* handle_packet's check */
        int data_length = packet_data_length(p, len);
        if (data_length < 0) {
            fail("the device rejects it: the length field is more than the packet holds");
            continue;
        }
        if ((size_t)data_length + 3 != len) fail("the length field does not match the packet");

        config_element_iterator it;
        config_element_iterator_init(&it, p + 3, data_length);
        while (it.next(&it))
            if (it.flags & ~MSG_FLAG_OUTPUT) fail("a message's flags set a bit other than MSG_FLAG_OUTPUT");
        if (it.pos != (size_t)data_length) fail("the messages do not fill the payload");

        switch (p[0]) {
        case PKT_CONFIG:        check_config(p, len); break;
        case PKT_COMMAND:       check_command(p, len); break;
        case PKT_UPDATE_CONFIG: check_update(p, len, false); break;
        case PKT_UPDATE_SECRET: check_update(p, len, true); break;
        default:                fail("not a packet type the UI should send"); break;
        }
    }
    fclose(f);

    if (packets == 0) {
        printf("no packets in %s\n", argv[1]);
        return 1;
    }
    printf("check_packets: %d packets, %d problems\n", packets, failures);
    return failures != 0;
}
