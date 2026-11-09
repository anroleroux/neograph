/*
 * Checks the examples in PROTOCOL.md against the protocol constants and helpers.
 * tests/protocol_examples.h is generated from PROTOCOL.md by make/examples.awk.
 */

#include <math.h>

#include "unity.h"
#include "protocol.h"
#include "program.h"
#include "ds18b20.h"
#include "encoder.h"
#include "clock.h"
#include "neopixel.h"
#include "servo.h"
#include "stepper.h"
#include "config/config_iterator.h"
#include "protocol_examples.h"

void setUp(void) {}
void tearDown(void) {}

static const struct protocol_example *example(const char *name)
{
    for (size_t i = 0; i < PROTOCOL_EXAMPLE_COUNT; i++)
        if (strcmp(protocol_examples[i].name, name) == 0) return &protocol_examples[i];
    TEST_FAIL_MESSAGE(name);
    return NULL;
}

static const struct fifo_example *fifo_example(const char *name)
{
    for (size_t i = 0; i < FIFO_EXAMPLE_COUNT; i++)
        if (strcmp(fifo_examples[i].name, name) == 0) return &fifo_examples[i];
    TEST_FAIL_MESSAGE(name);
    return NULL;
}

/* Iterate the messages of a packet example, starting after the packet header. */
static void messages(config_element_iterator *it, const struct protocol_example *ex)
{
    config_element_iterator_init(it, ex->bytes + PKT_HEADER_LEN, ex->len - PKT_HEADER_LEN);
}

static void assert_message(config_element_iterator *it, uint8_t type, uint16_t id, uint8_t len)
{
    TEST_ASSERT_TRUE(it->next(it));
    TEST_ASSERT_EQUAL_HEX8(type, it->type);
    TEST_ASSERT_EQUAL_UINT16(id, it->id);
    TEST_ASSERT_EQUAL_UINT8(len, it->len);
}

/* ── Every packet example ─────────────────────────────────────────────── */

void test_every_packet_example_is_well_formed(void)
{
    TEST_ASSERT_TRUE(PROTOCOL_EXAMPLE_COUNT > 0);

    for (size_t i = 0; i < PROTOCOL_EXAMPLE_COUNT; i++) {
        const struct protocol_example *ex = &protocol_examples[i];
        const uint8_t *b = ex->bytes;

        TEST_ASSERT_TRUE_MESSAGE(ex->len >= PKT_HEADER_LEN, ex->name);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(ex->len - PKT_HEADER_LEN, (b[1] << 8) | b[2], ex->name);

        switch (b[0]) {
        case PKT_DEVICE_ID:
            TEST_ASSERT_EQUAL_UINT_MESSAGE(8, ex->len - PKT_HEADER_LEN, ex->name);
            continue;   /* raw DID, no messages */
        case PKT_COMMAND:
        case PKT_CONFIG:
        case PKT_UPDATE_CONFIG:
        case PKT_UPDATE_SECRET:
            break;
        default:
            TEST_FAIL_MESSAGE(ex->name);
        }

        config_element_iterator it;
        messages(&it, ex);
        size_t count = 0;
        while (it.next(&it)) {
            TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, it.flags & ~(MSG_FLAG_OUTPUT | MSG_FLAG_SHOWN | MSG_FLAG_PART), ex->name);   /* unknown flags */
            count++;
        }
        TEST_ASSERT_TRUE_MESSAGE(count > 0, ex->name);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(ex->len - PKT_HEADER_LEN, it.pos, ex->name);

        if (b[0] == PKT_UPDATE_CONFIG || b[0] == PKT_UPDATE_SECRET)
            TEST_ASSERT_EQUAL_UINT_MESSAGE(1, count, ex->name);
    }
}

/* ── Specific examples ────────────────────────────────────────────────── */

void test_device_id(void)
{
    const struct protocol_example *ex = example("device-id");
    TEST_ASSERT_EQUAL_HEX8(PKT_DEVICE_ID, ex->bytes[0]);
}

void test_command_button(void)
{
    const struct protocol_example *ex = example("command-button");
    TEST_ASSERT_EQUAL_HEX8(PKT_COMMAND, ex->bytes[0]);

    config_element_iterator it;
    messages(&it, ex);
    assert_message(&it, MSG_UI_BUTTON, 1, 1);
    TEST_ASSERT_EQUAL_HEX8(VALUE_FULL, it.pay[0]);                 /* pressed: 1 */
    TEST_ASSERT_FALSE(it.next(&it));
}

void test_command_slider(void)
{
    const struct protocol_example *ex = example("command-slider");
    TEST_ASSERT_EQUAL_HEX8(PKT_COMMAND, ex->bytes[0]);

    config_element_iterator it;
    messages(&it, ex);
    assert_message(&it, MSG_UI_SLIDER, 4, 1);
    TEST_ASSERT_EQUAL_UINT8(191, it.pay[0]);                       /* round(0.75 * 255) */
    TEST_ASSERT_FALSE(it.next(&it));
}

void test_command_slider_2d(void)
{
    const struct protocol_example *ex = example("command-slider-2d");
    TEST_ASSERT_EQUAL_HEX8(PKT_COMMAND, ex->bytes[0]);

    config_element_iterator it;
    messages(&it, ex);
    assert_message(&it, MSG_UI_SLIDER_2D, 6, 2);                   /* one byte per output */
    TEST_ASSERT_EQUAL_HEX8(VALUE_FULL, it.pay[0]);                 /* X: 1 */
    TEST_ASSERT_EQUAL_HEX8(VALUE_HALF, it.pay[1]);                 /* Y: 0.5 */
    TEST_ASSERT_FALSE(it.next(&it));
}

void test_update_examples(void)
{
    config_element_iterator it;

    const struct protocol_example *ssid = example("update-ssid");
    TEST_ASSERT_EQUAL_HEX8(PKT_UPDATE_CONFIG, ssid->bytes[0]);
    messages(&it, ssid);
    assert_message(&it, MSG_NETWORK_FIELD, 0, 3);
    TEST_ASSERT_EQUAL_HEX8(FIELD_NETWORK_SSID, it.pay[0]);
    TEST_ASSERT_EQUAL_MEMORY("XY", it.pay + 1, 2);

    const struct protocol_example *opts = example("update-options");
    TEST_ASSERT_EQUAL_HEX8(PKT_UPDATE_CONFIG, opts->bytes[0]);
    messages(&it, opts);
    assert_message(&it, MSG_NETWORK, 0, 2);
    TEST_ASSERT_EQUAL_HEX8(FIELD_NETWORK_OPTIONS, it.pay[0]);
    TEST_ASSERT_EQUAL_HEX8(0, it.pay[1] & NET_OPT_JOIN);

    const struct protocol_example *pass = example("update-password");
    TEST_ASSERT_EQUAL_HEX8(PKT_UPDATE_SECRET, pass->bytes[0]);
    messages(&it, pass);
    assert_message(&it, MSG_NETWORK_FIELD, 0, 9);
    TEST_ASSERT_EQUAL_HEX8(FIELD_NETWORK_PASSWORD, it.pay[0]);
    TEST_ASSERT_EQUAL_MEMORY("newpass1", it.pay + 1, 8);
}

/* The networking messages written on first boot (build_config in src/config.c). */
static void assert_default_networking(config_element_iterator *it)
{
    assert_message(it, MSG_DEVICE, 0, 9);
    TEST_ASSERT_EQUAL_HEX8(FIELD_DEVICE_DID, it->pay[0]);
    assert_message(it, MSG_DEVICE_FIELD, 0, 5);
    TEST_ASSERT_EQUAL_HEX8(FIELD_DEVICE_NAME, it->pay[0]);
    assert_message(it, MSG_DEVICE_FIELD, 0, 3);
    TEST_ASSERT_EQUAL_HEX8(FIELD_DEVICE_NETWORK, it->pay[0]);
    TEST_ASSERT_EQUAL_UINT16(0, it->pay[1] << 8 | it->pay[2]);
    assert_message(it, MSG_NETWORK, 0, 2);
    TEST_ASSERT_EQUAL_HEX8(FIELD_NETWORK_OPTIONS, it->pay[0]);
    TEST_ASSERT_EQUAL_HEX8(NET_OPT_JOIN, it->pay[1]);
    assert_message(it, MSG_NETWORK_FIELD, 0, 5);
    TEST_ASSERT_EQUAL_HEX8(FIELD_NETWORK_NAME, it->pay[0]);
    assert_message(it, MSG_NETWORK_FIELD, 0, 4);
    TEST_ASSERT_EQUAL_HEX8(FIELD_NETWORK_SSID, it->pay[0]);
    TEST_ASSERT_EQUAL_MEMORY("LRX", it->pay + 1, 3);
}

void test_default_config(void)
{
    const struct protocol_example *ex = example("default-config");
    TEST_ASSERT_EQUAL_HEX8(PKT_CONFIG, ex->bytes[0]);

    config_element_iterator it;
    messages(&it, ex);
    assert_default_networking(&it);
    TEST_ASSERT_FALSE(it.next(&it));
}

void test_save_config_keeps_device_config_then_appends_graph(void)
{
    const struct protocol_example *ex = example("save-config");
    const struct protocol_example *dflt = example("default-config");
    TEST_ASSERT_EQUAL_HEX8(PKT_CONFIG, ex->bytes[0]);

    /* The device's config payload, byte for byte, comes first. */
    TEST_ASSERT_EQUAL_MEMORY(dflt->bytes + PKT_HEADER_LEN, ex->bytes + PKT_HEADER_LEN,
                             dflt->len - PKT_HEADER_LEN);

    config_element_iterator it;
    messages(&it, ex);
    assert_default_networking(&it);

    /* Each node: its control message, its label, then its position. */
    assert_message(&it, MSG_UI_BUTTON, 1, 0);
    assert_message(&it, MSG_STRING, 1, 5);
    TEST_ASSERT_EQUAL_MEMORY("Start", it.pay, 5);
    assert_message(&it, MSG_LAYOUT, 1, 4);
    TEST_ASSERT_EQUAL_INT16(-200, (int16_t)(it.pay[0] << 8 | it.pay[1]));
    TEST_ASSERT_EQUAL_INT16(-40, (int16_t)(it.pay[2] << 8 | it.pay[3]));

    assert_message(&it, MSG_HW_LED, 2, 3);
    TEST_ASSERT_EQUAL_HEX8(MSG_FLAG_OUTPUT, it.flags);
    TEST_ASSERT_EQUAL_UINT16(1, msg_ref_at(it.pay, it.flags).id);  /* source n1 */
    TEST_ASSERT_EQUAL_UINT8(0, msg_ref_at(it.pay, it.flags).output);
    assert_message(&it, MSG_STRING, 2, 12);
    TEST_ASSERT_EQUAL_MEMORY("Pompe \xC3\xA0 eau", it.pay, 12);   /* UTF-8 */
    assert_message(&it, MSG_LAYOUT, 2, 4);
    TEST_ASSERT_EQUAL_INT16(100, (int16_t)(it.pay[0] << 8 | it.pay[1]));
    TEST_ASSERT_EQUAL_INT16(40, (int16_t)(it.pay[2] << 8 | it.pay[3]));
    assert_message(&it, MSG_WIRE, 2, 4);                          /* wire n1 -> n2 */
    TEST_ASSERT_EQUAL_HEX16(0xF562, it.pay[0] << 8 | it.pay[1]);
    TEST_ASSERT_EQUAL_HEX16(0x7562, it.pay[2] << 8 | it.pay[3]);

    assert_message(&it, MSG_HW_GPO, 3, 4);
    TEST_ASSERT_EQUAL_HEX8(MSG_FLAG_OUTPUT, it.flags);
    TEST_ASSERT_EQUAL_UINT16(1, msg_ref_at(it.pay, it.flags).id);  /* source n1 */
    TEST_ASSERT_EQUAL_UINT8(1, it.pay[3]);                         /* pin 1 */
    assert_message(&it, MSG_STRING, 3, 3);
    TEST_ASSERT_EQUAL_MEMORY("GPO", it.pay, 3);
    assert_message(&it, MSG_LAYOUT, 3, 4);
    TEST_ASSERT_EQUAL_INT16(200, (int16_t)(it.pay[0] << 8 | it.pay[1]));
    TEST_ASSERT_EQUAL_INT16(0, (int16_t)(it.pay[2] << 8 | it.pay[3]));
    assert_message(&it, MSG_WIRE, 3, 4);                          /* wire n1 -> n3 */
    TEST_ASSERT_EQUAL_HEX16(0xFBF0, it.pay[0] << 8 | it.pay[1]);
    TEST_ASSERT_EQUAL_HEX16(0x7BF0, it.pay[2] << 8 | it.pay[3]);

    assert_message(&it, MSG_HW_PWM, 4, 17);
    pwm_payload pwm;
    TEST_ASSERT_TRUE(pwm_payload_read(it.pay, it.len, it.flags, &pwm));
    TEST_ASSERT_EQUAL_UINT16(5, pwm.duty_source.id);               /* duty source n5 */
    TEST_ASSERT_EQUAL_UINT8(0, pwm.duty_source.output);
    TEST_ASSERT_EQUAL_UINT8(2, pwm.pin);                           /* pin 2 */
    TEST_ASSERT_EQUAL_UINT16(1000, pwm.freq);                      /* 1000 Hz */
    TEST_ASSERT_EQUAL_HEX8(PWM_DUTY_INPUT, pwm.options);           /* duty from input, constant frequency */
    TEST_ASSERT_EQUAL_UINT8(50, pwm.duty);                         /* constant duty 50 % */
    TEST_ASSERT_EQUAL_UINT8(0, pwm.duty_min);                      /* duty at 0: 0 % */
    TEST_ASSERT_EQUAL_UINT8(100, pwm.duty_max);                    /* duty at 1: 100 % */
    TEST_ASSERT_EQUAL_UINT16(0, pwm.freq_source.id);               /* no frequency source */
    TEST_ASSERT_EQUAL_UINT16(100, pwm.freq_min);                   /* frequency at 0 */
    TEST_ASSERT_EQUAL_UINT16(10000, pwm.freq_max);                 /* frequency at 1 */
    assert_message(&it, MSG_STRING, 4, 3);
    TEST_ASSERT_EQUAL_MEMORY("PWM", it.pay, 3);
    assert_message(&it, MSG_LAYOUT, 4, 4);
    TEST_ASSERT_EQUAL_INT16(200, (int16_t)(it.pay[0] << 8 | it.pay[1]));
    TEST_ASSERT_EQUAL_INT16(120, (int16_t)(it.pay[2] << 8 | it.pay[3]));
    assert_message(&it, MSG_WIRE, 4, 7);                          /* wire n5 -> n4, with its source */
    TEST_ASSERT_EQUAL_HEX16(0x0000, it.pay[0] << 8 | it.pay[1]);
    TEST_ASSERT_EQUAL_HEX16(0x8000, it.pay[2] << 8 | it.pay[3]);
    TEST_ASSERT_EQUAL_UINT16(5, msg_ref_at(it.pay + 4, it.flags).id);
    TEST_ASSERT_EQUAL_UINT8(0, msg_ref_at(it.pay + 4, it.flags).output);

    assert_message(&it, MSG_UI_SLIDER, 5, 1);
    TEST_ASSERT_EQUAL_HEX8(0, it.pay[0] & SLIDER_VERTICAL);        /* horizontal */
    assert_message(&it, MSG_STRING, 5, 5);
    TEST_ASSERT_EQUAL_MEMORY("Level", it.pay, 5);
    assert_message(&it, MSG_LAYOUT, 5, 4);
    TEST_ASSERT_EQUAL_INT16(-200, (int16_t)(it.pay[0] << 8 | it.pay[1]));
    TEST_ASSERT_EQUAL_INT16(120, (int16_t)(it.pay[2] << 8 | it.pay[3]));

    TEST_ASSERT_FALSE(it.next(&it));
}

/* Two source references are to the same output of the same element. */
static bool same_ref(msg_ref a, msg_ref b) { return a.id == b.id && a.output == b.output; }

/* Every label, position and wire in a config example describes a control element in
 * the same config. Positions are exactly 4 bytes. A wire belongs to an output with a
 * source: 4 bytes into an LED or GPO, 4 and a source reference into a PWM, which is
 * one of its sources. */
void test_graph_messages_belong_to_controls(void)
{
    for (size_t i = 0; i < PROTOCOL_EXAMPLE_COUNT; i++) {
        const struct protocol_example *ex = &protocol_examples[i];
        if (ex->bytes[0] != PKT_CONFIG) continue;

        config_element_iterator labels, controls;
        messages(&labels, ex);
        while (labels.next(&labels)) {
            if (labels.type != MSG_STRING && labels.type != MSG_LAYOUT && labels.type != MSG_WIRE) continue;
            if (labels.type == MSG_STRING) TEST_ASSERT_TRUE_MESSAGE(labels.len > 0, ex->name);
            if (labels.type == MSG_LAYOUT) TEST_ASSERT_EQUAL_UINT8_MESSAGE(4, labels.len, ex->name);

            bool found = false;
            messages(&controls, ex);
            while (controls.next(&controls)) {
                if (controls.type >= 0x80 || controls.id != labels.id) continue;
                pwm_payload pwm;
                if (labels.type != MSG_WIRE)
                    found = true;
                else if (controls.type == MSG_HW_NEOPIXEL) {
                    /* Two inputs: each wire names its source. */
                    neopixel_payload px;
                    TEST_ASSERT_TRUE_MESSAGE(neopixel_payload_read(controls.pay, controls.len, controls.flags, &px), ex->name);
                    msg_ref wired = msg_ref_at(labels.pay + 4, labels.flags);
                    found = labels.len == 4 + msg_ref_len(labels.flags) && wired.id != 0
                        && (same_ref(wired, px.source) || same_ref(wired, px.enable));
                }
                else if (controls.type == MSG_HW_LED || controls.type == MSG_HW_GPO || controls.type == MSG_HW_STEPPER
                         || controls.type == MSG_HW_SERVO)
                    found = labels.len == 4 && msg_ref_at(controls.pay, controls.flags).id != 0;
                else if (controls.type == MSG_HW_PWM && pwm_payload_read(controls.pay, controls.len, controls.flags, &pwm)) {
                    msg_ref wired = msg_ref_at(labels.pay + 4, labels.flags);
                    found = labels.len == 4 + msg_ref_len(labels.flags) && wired.id != 0
                        && (same_ref(wired, pwm.duty_source) || same_ref(wired, pwm.freq_source));
                }
            }
            TEST_ASSERT_TRUE_MESSAGE(found, ex->name);
        }
    }
}

/* The type of a source element in a config example; 0 if it is not there. */
static uint8_t element_type(const struct protocol_example *ex, uint16_t id)
{
    config_element_iterator it;
    messages(&it, ex);
    while (it.next(&it))
        if (it.type < 0x80 && it.id == id) return it.type;
    return 0;
}

/* What a reference to a block's copy's output stands for: its entry in the copy's table. */
static msg_ref through_block(const struct protocol_example *ex, msg_ref src)
{
    config_element_iterator it;
    messages(&it, ex);
    while (it.next(&it)) {
        if (it.type != MSG_SUB || it.id != src.id || !src.id) continue;
        TEST_ASSERT_TRUE_MESSAGE(sub_payload_valid(it.pay, it.len, it.flags) && src.output < it.pay[4], ex->name);
        return msg_ref_at(it.pay + 5 + 3 * (it.pay[3] + src.output), it.flags);
    }
    return src;
}

/* A floating output the device has: a slider's, a 2D slider's X or Y, an ADC's or the core temperature's. */
static void assert_floating_hw(const struct protocol_example *ex, msg_ref src)
{
    if (src.id == 0) return;
    uint8_t type = element_type(ex, src.id);
    TEST_ASSERT_TRUE_MESSAGE(type == MSG_UI_SLIDER || type == MSG_UI_SLIDER_2D || type == MSG_HW_ADC
                             || type == MSG_HW_CORE || type == MSG_HW_DS18B20 || type == MSG_HW_ENCODER
                             || type == MSG_HW_CLOCK || program_logic_floating(type), ex->name);
    /* A DS18B20's temperature, an encoder's position: their output 0; a clock's three. */
    TEST_ASSERT_TRUE_MESSAGE(src.output < (type == MSG_UI_SLIDER_2D ? 2 : type == MSG_HW_CLOCK ? 3 : 1), ex->name);
}

/* Wires join matching types (PROTOCOL.md, "Wire types"): an LED, GPO or keyboard is
 * driven by a button, a GPIO input or logic, a PWM or compare by sliders, 2D sliders, ADCs or
 * the core temperature. */
void test_wires_join_matching_types(void)
{
    for (size_t i = 0; i < PROTOCOL_EXAMPLE_COUNT; i++) {
        const struct protocol_example *ex = &protocol_examples[i];
        if (ex->bytes[0] != PKT_CONFIG) continue;

        config_element_iterator it;
        messages(&it, ex);
        while (it.next(&it)) {
            bool floating_in = it.type == MSG_LOGIC_COMPARE || it.type == MSG_HW_STEPPER || it.type == MSG_HW_SERVO
                || (program_logic_floating(it.type) && it.type != MSG_LOGIC_SINE);   /* a sine's input is on/off */
            bool boolean_in = it.type == MSG_HW_LED || it.type == MSG_HW_GPO || it.type == MSG_HW_KEYBOARD
                || it.type == MSG_UI_INDICATOR || (program_logic(it.type) && !floating_in);
            uint8_t r = msg_ref_len(it.flags);
            uint8_t refs = it.type == MSG_LOGIC_AND || it.type == MSG_LOGIC_OR || it.type == MSG_LOGIC_MIN
                || it.type == MSG_LOGIC_MAX ? it.len / r : it.type == MSG_LOGIC_LATCH ? 2 : 1;
            if (boolean_in) {
                /* Boolean sources: a button, a GPIO input or boolean logic. */
                for (uint8_t k = 0; k < refs; k++) {
                    msg_ref src = through_block(ex, msg_ref_at(it.pay + k * r, it.flags));
                    uint8_t type = element_type(ex, src.id);
                    if (src.id) TEST_ASSERT_TRUE_MESSAGE(type == MSG_UI_BUTTON || type == MSG_HW_GPI || type == MSG_HW_DS18B20
                                                         || type == MSG_HW_ENCODER
                                                         || (program_logic(type) && !program_logic_floating(type)), ex->name);
                    /* A DS18B20's found, an encoder's pressed, is its output 1; the others' only
                       boolean output is 0. */
                    TEST_ASSERT_EQUAL_UINT8_MESSAGE(type == MSG_HW_DS18B20 || type == MSG_HW_ENCODER ? 1 : 0,
                                                    src.output, ex->name);
                }
            } else if (floating_in) {
                for (uint8_t k = 0; k < refs; k++) assert_floating_hw(ex, msg_ref_at(it.pay + k * r, it.flags));
            } else if (it.type == MSG_HW_NEOPIXEL) {
                /* Its value from a floating source, its on/off from a boolean one. */
                neopixel_payload px;
                TEST_ASSERT_TRUE_MESSAGE(neopixel_payload_read(it.pay, it.len, it.flags, &px), ex->name);
                assert_floating_hw(ex, px.source);
                uint8_t type = element_type(ex, px.enable.id);
                if (px.enable.id) TEST_ASSERT_TRUE_MESSAGE(type == MSG_UI_BUTTON || type == MSG_HW_GPI
                                                           || (program_logic(type) && !program_logic_floating(type)), ex->name);
            } else if (it.type == MSG_HW_PWM) {
                pwm_payload pwm;
                TEST_ASSERT_TRUE_MESSAGE(pwm_payload_read(it.pay, it.len, it.flags, &pwm), ex->name);
                assert_floating_hw(ex, pwm.duty_source);
                assert_floating_hw(ex, pwm.freq_source);
            }
        }
    }
}

/* ── Element type bits ────────────────────────────────────────────────── */

void test_element_type_bits(void)
{
    TEST_ASSERT_EQUAL_HEX8(MSG_INPUT_BIT, MSG_UI_BUTTON & (MSG_HW_BIT | MSG_INPUT_BIT));
    TEST_ASSERT_EQUAL_HEX8(MSG_INPUT_BIT, MSG_UI_SLIDER & (MSG_HW_BIT | MSG_INPUT_BIT));
    TEST_ASSERT_EQUAL_HEX8(MSG_INPUT_BIT, MSG_UI_SLIDER_2D & (MSG_HW_BIT | MSG_INPUT_BIT));
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_BIT, MSG_HW_LED & (MSG_HW_BIT | MSG_INPUT_BIT));
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_BIT, MSG_HW_GPO & (MSG_HW_BIT | MSG_INPUT_BIT));
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_BIT, MSG_HW_PWM & (MSG_HW_BIT | MSG_INPUT_BIT));
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_BIT, MSG_HW_KEYBOARD & (MSG_HW_BIT | MSG_INPUT_BIT));
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_BIT, MSG_HW_STEPPER & (MSG_HW_BIT | MSG_INPUT_BIT));
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_BIT, MSG_HW_SERVO & (MSG_HW_BIT | MSG_INPUT_BIT));
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_BIT, MSG_HW_NEOPIXEL & (MSG_HW_BIT | MSG_INPUT_BIT));
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_BIT | MSG_INPUT_BIT, MSG_HW_GPI & (MSG_HW_BIT | MSG_INPUT_BIT));
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_BIT | MSG_INPUT_BIT, MSG_HW_ADC & (MSG_HW_BIT | MSG_INPUT_BIT));
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_BIT | MSG_INPUT_BIT, MSG_HW_DS18B20 & (MSG_HW_BIT | MSG_INPUT_BIT));
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_BIT | MSG_INPUT_BIT, MSG_HW_ENCODER & (MSG_HW_BIT | MSG_INPUT_BIT));
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_BIT | MSG_INPUT_BIT, MSG_HW_CLOCK & (MSG_HW_BIT | MSG_INPUT_BIT));
    TEST_ASSERT_EQUAL_HEX8(0, MSG_UI_GAUGE & (MSG_HW_BIT | MSG_INPUT_BIT));
    TEST_ASSERT_EQUAL_HEX8(0, MSG_UI_INDICATOR & (MSG_HW_BIT | MSG_INPUT_BIT));
}

/* GPIO 0-22 and 26-28 are usable; 23, 24, 25 and 29 drive the WiFi chip. */
void test_pin_usable(void)
{
    for (unsigned pin = 0; pin <= 255; pin++) {
        int want = pin <= 22 || (pin >= 26 && pin <= 28);
        TEST_ASSERT_EQUAL_INT_MESSAGE(want, pin_usable((uint8_t)pin), "pin_usable");
    }
}

/* ── Network addresses ────────────────────────────────────────────────── */

void test_update_ap_address(void)
{
    config_element_iterator it;
    messages(&it, example("update-ap-address"));
    assert_message(&it, MSG_NETWORK_FIELD, 0, 5);
    TEST_ASSERT_EQUAL_HEX8(FIELD_NETWORK_AP_ADDRESS, it.pay[0]);
    TEST_ASSERT_TRUE(net_ap_address_usable(it.pay + 1));
    TEST_ASSERT_FALSE(it.next(&it));
}

void test_net_address_usable(void)
{
    const uint8_t ok[][4] = { {192, 168, 4, 1}, {10, 0, 0, 1}, {172, 16, 5, 254}, {192, 168, 4, 15}, {192, 168, 4, 24} };
    for (size_t i = 0; i < sizeof ok / sizeof ok[0]; i++) {
        TEST_ASSERT_TRUE(net_address_usable(ok[i]));
        TEST_ASSERT_TRUE(net_ap_address_usable(ok[i]));
    }
    const uint8_t bad[][4] = { {0, 0, 0, 0}, {127, 0, 0, 1}, {224, 0, 0, 1}, {255, 255, 255, 255},
                               {192, 168, 4, 0}, {192, 168, 4, 255} };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        TEST_ASSERT_FALSE(net_address_usable(bad[i]));
        TEST_ASSERT_FALSE(net_ap_address_usable(bad[i]));
    }
    /* the access point's DHCP server leases .16 to .23: a fixed address on a joined
       network may be one of those, the access point may not */
    for (uint8_t host = NET_LEASE_FIRST; host < NET_LEASE_FIRST + NET_LEASE_COUNT; host++) {
        const uint8_t leased[4] = { 192, 168, 4, host };
        TEST_ASSERT_TRUE(net_address_usable(leased));
        TEST_ASSERT_FALSE(net_ap_address_usable(leased));
    }
}

/* ── USB keyboards ────────────────────────────────────────────────────── */

void test_keyboard_config(void)
{
    const struct protocol_example *ex = example("keyboard-config");
    config_element_iterator it;
    messages(&it, ex);

    assert_message(&it, MSG_UI_BUTTON, 1, 1);
    TEST_ASSERT_EQUAL_HEX8(BUTTON_MOMENTARY, it.pay[0]);
    assert_message(&it, MSG_HW_KEYBOARD, 2, 6);
    TEST_ASSERT_TRUE(keyboard_payload_valid(it.pay, it.len, it.flags));
    TEST_ASSERT_EQUAL_UINT16(1, msg_ref_at(it.pay, it.flags).id);
    TEST_ASSERT_EQUAL_HEX8(KEYBOARD_COMBO, it.pay[3]);
    TEST_ASSERT_EQUAL_HEX8(0x05, it.pay[4]);                     /* Ctrl + Alt */
    TEST_ASSERT_EQUAL_HEX8(0x4C, it.pay[5]);                     /* Delete */
    assert_message(&it, MSG_UI_BUTTON, 3, 0);                    /* a toggle */
    assert_message(&it, MSG_HW_KEYBOARD, 4, 10);
    TEST_ASSERT_TRUE(keyboard_payload_valid(it.pay, it.len, it.flags));
    TEST_ASSERT_EQUAL_HEX8(KEYBOARD_TEXT, it.pay[3]);
    TEST_ASSERT_EQUAL_MEMORY("hello\n", it.pay + 4, 6);
    TEST_ASSERT_FALSE(it.next(&it));
}

/* The payloads unmarshal_controls sets up (src/config.c): a source reference, then a
 * combo of 3 bytes, or text of 1 to KEYBOARD_TEXT_MAX characters the keyboard can type.
 * The reference is 3 bytes with MSG_FLAG_OUTPUT, 2 in configs saved without it. */
void test_keyboard_payload_valid(void)
{
    for (uint8_t flags = 0; flags <= MSG_FLAG_OUTPUT; flags++) {
        uint8_t r = msg_ref_len(flags);
        uint8_t p[3 + 1 + KEYBOARD_TEXT_MAX + 1] = { 0x00, 0x01, 0x00 };
        uint8_t *k = p + r;
        k[0] = KEYBOARD_COMBO; k[1] = 0x08; k[2] = 0x0F;
        TEST_ASSERT_TRUE(keyboard_payload_valid(p, r + 3, flags));
        TEST_ASSERT_FALSE(keyboard_payload_valid(p, r + 2, flags));
        TEST_ASSERT_FALSE(keyboard_payload_valid(p, r + 4, flags));
        TEST_ASSERT_FALSE(keyboard_payload_valid(p, r, flags));

        k[0] = KEYBOARD_TEXT;
        memset(k + 1, 'a', KEYBOARD_TEXT_MAX + 1);
        TEST_ASSERT_FALSE(keyboard_payload_valid(p, r + 1, flags));                  /* no text */
        TEST_ASSERT_TRUE(keyboard_payload_valid(p, r + 2, flags));
        TEST_ASSERT_TRUE(keyboard_payload_valid(p, r + 1 + KEYBOARD_TEXT_MAX, flags));
        TEST_ASSERT_FALSE(keyboard_payload_valid(p, r + 1 + KEYBOARD_TEXT_MAX + 1, flags));
        k[1] = '\t'; k[2] = '\n'; k[3] = ' '; k[4] = '~';
        TEST_ASSERT_TRUE(keyboard_payload_valid(p, r + 5, flags));
        k[2] = '\r';
        TEST_ASSERT_FALSE(keyboard_payload_valid(p, r + 5, flags));
        k[2] = 0x7F;
        TEST_ASSERT_FALSE(keyboard_payload_valid(p, r + 5, flags));
        k[2] = 0xC3;                                                              /* UTF-8 */
        TEST_ASSERT_FALSE(keyboard_payload_valid(p, r + 5, flags));

        k[0] = 0x02;                                                              /* no such mode */
        TEST_ASSERT_FALSE(keyboard_payload_valid(p, r + 3, flags));
    }
}

/* ── Source references and 2D sliders ─────────────────────────────────── */

void test_slider_2d_config(void)
{
    config_element_iterator it;
    messages(&it, example("slider-2d-config"));

    assert_message(&it, MSG_UI_SLIDER_2D, 1, 4);
    TEST_ASSERT_EQUAL_HEX8(SLIDER_RETURN | SLIDER_EASE_LINEAR, it.pay[0]);
    TEST_ASSERT_EQUAL_UINT8(100, it.pay[1]);                      /* return speed, %/s */
    uint8_t home[2];
    slider_2d_home(it.pay, it.len, home);
    TEST_ASSERT_EQUAL_HEX8(0x40, home[0]);                        /* X 0.25 */
    TEST_ASSERT_EQUAL_HEX8(0xBF, home[1]);                        /* Y 0.75 */
    slider_2d_home(it.pay, 2, home);                              /* saved without one: the centre */
    TEST_ASSERT_EQUAL_HEX8(VALUE_HALF, home[0]);
    TEST_ASSERT_EQUAL_HEX8(VALUE_HALF, home[1]);
    TEST_ASSERT_EQUAL_HEX8(0, SLIDER_RETURN & SLIDER_EASE_MASK);
    TEST_ASSERT_EQUAL_HEX8(SLIDER_EASE_LINEAR | SLIDER_EASE_SIGMOID, SLIDER_EASE_MASK);
    assert_message(&it, MSG_HW_PWM, 2, 17);
    pwm_payload pwm;
    TEST_ASSERT_TRUE(pwm_payload_read(it.pay, it.len, it.flags, &pwm));
    TEST_ASSERT_EQUAL_UINT16(1, pwm.duty_source.id);
    TEST_ASSERT_EQUAL_UINT8(0, pwm.duty_source.output);           /* X */
    TEST_ASSERT_EQUAL_UINT16(1, pwm.freq_source.id);
    TEST_ASSERT_EQUAL_UINT8(1, pwm.freq_source.output);           /* Y */
    TEST_ASSERT_EQUAL_HEX8(PWM_DUTY_INPUT | PWM_FREQ_INPUT, pwm.options);
    TEST_ASSERT_FALSE(it.next(&it));
}

void test_gpi_config(void)
{
    config_element_iterator it;
    messages(&it, example("gpi-config"));

    assert_message(&it, MSG_HW_GPI, 1, 2);
    TEST_ASSERT_TRUE(pin_usable(it.pay[0]));
    TEST_ASSERT_EQUAL_UINT8(3, it.pay[0]);
    TEST_ASSERT_EQUAL_HEX8(GPI_PULL_UP, it.pay[1] & GPI_PULL_MASK);
    TEST_ASSERT_EQUAL_HEX8(GPI_INVERT, it.pay[1] & GPI_INVERT);
    assert_message(&it, MSG_HW_LED, 2, 3);
    assert_message(&it, MSG_UI_INDICATOR, 3, 3);
    TEST_ASSERT_FALSE(it.next(&it));
}

void test_light_config(void)
{
    config_element_iterator it;
    messages(&it, example("light-config"));

    assert_message(&it, MSG_UI_BUTTON, 1, 1);
    assert_message(&it, MSG_UI_INDICATOR, 2, 3);
    TEST_ASSERT_EQUAL_HEX8(MSG_FLAG_OUTPUT, it.flags);
    TEST_ASSERT_EQUAL_UINT16(1, msg_ref_at(it.pay, it.flags).id);
    TEST_ASSERT_EQUAL_UINT8(0, msg_ref_at(it.pay, it.flags).output);
    TEST_ASSERT_FALSE(it.next(&it));
}

/* A config saved before outputs: 2-byte references, read as output 0. */
void test_legacy_config(void)
{
    config_element_iterator it;
    messages(&it, example("legacy-config"));

    assert_message(&it, MSG_UI_BUTTON, 1, 0);
    assert_message(&it, MSG_HW_LED, 2, 2);
    TEST_ASSERT_EQUAL_HEX8(0, it.flags);
    TEST_ASSERT_EQUAL_UINT8(2, msg_ref_len(it.flags));
    TEST_ASSERT_EQUAL_UINT16(1, msg_ref_at(it.pay, it.flags).id);
    TEST_ASSERT_EQUAL_UINT8(0, msg_ref_at(it.pay, it.flags).output);
    TEST_ASSERT_FALSE(it.next(&it));

    /* A PWM's payload is 15 bytes without outputs, 17 with them. */
    const uint8_t old_pwm[15] = { 0, 5, 2, 0x03, 0xE8, PWM_DUTY_INPUT | PWM_FREQ_INPUT, 50, 0, 100,
                                  0, 6, 0, 100, 0x27, 0x10 };
    pwm_payload pwm;
    TEST_ASSERT_FALSE(pwm_payload_read(old_pwm, 15, MSG_FLAG_OUTPUT, &pwm));
    TEST_ASSERT_TRUE(pwm_payload_read(old_pwm, 15, 0, &pwm));
    TEST_ASSERT_EQUAL_UINT16(5, pwm.duty_source.id);
    TEST_ASSERT_EQUAL_UINT8(2, pwm.pin);
    TEST_ASSERT_EQUAL_UINT16(1000, pwm.freq);
    TEST_ASSERT_EQUAL_UINT16(6, pwm.freq_source.id);
    TEST_ASSERT_EQUAL_UINT8(0, pwm.freq_source.output);
    TEST_ASSERT_EQUAL_UINT16(100, pwm.freq_min);
    TEST_ASSERT_EQUAL_UINT16(10000, pwm.freq_max);
}

/* ── FIFO words ───────────────────────────────────────────────────────── */

/* ── Programs ─────────────────────────────────────────────────────────── */

/* Each `program` example is what program_compile (src/program.c) makes of the
 * config example of the same name: core 0 compiles it, core 1 runs it. */
void test_program_examples_match_their_configs(void)
{
    TEST_ASSERT_TRUE(PROGRAM_EXAMPLE_COUNT > 0);
    for (size_t i = 0; i < PROGRAM_EXAMPLE_COUNT; i++) {
        const struct program_example *ex = &program_examples[i];
        const struct protocol_example *cfg = example(ex->name);
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(PKT_CONFIG, cfg->bytes[0], ex->name);

        static hw_program p;
        program_compile(cfg->bytes + PKT_HEADER_LEN, cfg->len - PKT_HEADER_LEN, &p);
        char text[2048];
        TEST_ASSERT_TRUE_MESSAGE(program_describe(&p, text, sizeof text) < sizeof text, ex->name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(ex->text, text, ex->name);
    }
}

/* Every config example with something core 1 runs has its program in PROTOCOL.md. */
void test_every_config_example_with_hardware_has_a_program(void)
{
    for (size_t i = 0; i < PROTOCOL_EXAMPLE_COUNT; i++) {
        const struct protocol_example *cfg = &protocol_examples[i];
        if (cfg->bytes[0] != PKT_CONFIG) continue;
        static hw_program p;
        program_compile(cfg->bytes + PKT_HEADER_LEN, cfg->len - PKT_HEADER_LEN, &p);
        if (p.node_count == 0) continue;
        size_t j = 0;
        while (j < PROGRAM_EXAMPLE_COUNT && strcmp(program_examples[j].name, cfg->name) != 0) j++;
        TEST_ASSERT_TRUE_MESSAGE(j < PROGRAM_EXAMPLE_COUNT, cfg->name);
    }
}

/* A source's signal is found by its element and output; a missing one is PROG_NO_SIGNAL,
 * and an output wired to it reads nothing. A repeated ID keeps one signal. */
void test_program_signals(void)
{
    static const uint8_t cfg[] = {
        MSG_UI_SLIDER_2D, 0, 0, 7, 4, 0x01, 100, 0x00, 0xFF,    /* 2D slider n7, home (0, 1) */
        MSG_UI_BUTTON, 0, 0, 1, 0,                              /* button n1 */
        MSG_UI_BUTTON, 0, 0, 1, 1, 0x01,                        /* button n1 again */
        MSG_HW_GPO, MSG_FLAG_OUTPUT, 0, 2, 4, 0, 9, 0, 5,       /* GPO n2 on GPIO 5, from n9: not there */
    };
    static hw_program p;
    program_compile(cfg, sizeof cfg, &p);
    TEST_ASSERT_EQUAL_UINT8(3, p.signal_count);
    TEST_ASSERT_EQUAL_UINT8(0, program_signal(&p, 7, 0));
    TEST_ASSERT_EQUAL_UINT8(1, program_signal(&p, 7, 1));
    TEST_ASSERT_EQUAL_UINT8(2, program_signal(&p, 1, 0));
    TEST_ASSERT_EQUAL_UINT8(PROG_NO_SIGNAL, program_signal(&p, 1, 1));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p.signals[0].value);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, p.signals[1].value);
    TEST_ASSERT_TRUE(program_ui_source(p.signals[2].type));
    TEST_ASSERT_EQUAL_UINT8(1, p.node_count);
    TEST_ASSERT_EQUAL_UINT8(PROG_NO_SIGNAL, p.nodes[0].in[0]);
}

/* Too many sources or nodes: the ones over the limit are left out. */
void test_program_limits(void)
{
    static uint8_t cfg[PKT_CONFIG_MAX];
    size_t n = 0;
    for (uint16_t id = 1; id <= PROG_SIGNALS_MAX + 10; id++) {
        uint8_t m[] = { MSG_HW_CORE, 0, (uint8_t)(id >> 8), (uint8_t)id, 0 };
        memcpy(cfg + n, m, sizeof m);
        n += sizeof m;
    }
    static hw_program p;
    program_compile(cfg, n, &p);
    TEST_ASSERT_EQUAL_UINT8(PROG_SIGNALS_MAX, p.signal_count);
    TEST_ASSERT_EQUAL_UINT8(PROG_NODES_MAX, p.node_count);
}

/* ── Logic ────────────────────────────────────────────────────────────── */

/* PROTOCOL.md, "Logic": core temperature above 40 °C, with 1 °C of hysteresis, drives GPIO 5. */
void test_logic_config(void)
{
    config_element_iterator it;
    messages(&it, example("logic-config"));

    assert_message(&it, MSG_HW_CORE, 1, 0);
    assert_message(&it, MSG_LOGIC_COMPARE, 2, 12);
    TEST_ASSERT_TRUE(logic_payload_valid(it.type, it.pay, it.len, it.flags));
    TEST_ASSERT_EQUAL_UINT16(1, msg_ref_at(it.pay, it.flags).id);
    TEST_ASSERT_EQUAL_HEX8(COMPARE_ABOVE, it.pay[3]);
    TEST_ASSERT_EQUAL_FLOAT(40.0f, msg_float_at(it.pay + 4));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, msg_float_at(it.pay + 8));
    assert_message(&it, MSG_HW_GPO, 3, 4);
    TEST_ASSERT_EQUAL_UINT16(2, msg_ref_at(it.pay, it.flags).id);
    TEST_ASSERT_EQUAL_UINT8(5, it.pay[3]);
    TEST_ASSERT_FALSE(it.next(&it));
}

void test_logic_payload_valid(void)
{
    const uint8_t F = MSG_FLAG_OUTPUT;
    /* a compare: a source, its condition, threshold 40 and hysteresis 1 */
    uint8_t compare[] = { 0, 1, 0, COMPARE_ABOVE, 0x42, 0x20, 0, 0, 0x3F, 0x80, 0, 0 };
    TEST_ASSERT_TRUE(logic_payload_valid(MSG_LOGIC_COMPARE, compare, sizeof compare, F));
    TEST_ASSERT_FALSE(logic_payload_valid(MSG_LOGIC_COMPARE, compare, sizeof compare - 1, F));
    compare[3] = 2;                                                     /* no such condition */
    TEST_ASSERT_FALSE(logic_payload_valid(MSG_LOGIC_COMPARE, compare, sizeof compare, F));
    compare[3] = COMPARE_BELOW;
    compare[8] = 0xBF;                                                  /* hysteresis -1 */
    TEST_ASSERT_FALSE(logic_payload_valid(MSG_LOGIC_COMPARE, compare, sizeof compare, F));
    compare[8] = 0x7F; compare[9] = 0x80;                               /* hysteresis infinite */
    TEST_ASSERT_FALSE(logic_payload_valid(MSG_LOGIC_COMPARE, compare, sizeof compare, F));
    compare[8] = 0x3F; compare[4] = 0x7F; compare[5] = 0xC0;            /* threshold NaN */
    TEST_ASSERT_FALSE(logic_payload_valid(MSG_LOGIC_COMPARE, compare, sizeof compare, F));

    /* an AND or OR: 1 to LOGIC_INPUTS_MAX sources */
    const uint8_t refs[3 * (LOGIC_INPUTS_MAX + 1)] = { 0 };
    TEST_ASSERT_FALSE(logic_payload_valid(MSG_LOGIC_AND, refs, 0, F));
    for (uint8_t n = 1; n <= LOGIC_INPUTS_MAX; n++) TEST_ASSERT_TRUE(logic_payload_valid(MSG_LOGIC_OR, refs, 3 * n, F));
    TEST_ASSERT_FALSE(logic_payload_valid(MSG_LOGIC_AND, refs, 3 * (LOGIC_INPUTS_MAX + 1), F));
    TEST_ASSERT_FALSE(logic_payload_valid(MSG_LOGIC_AND, refs, 4, F));
    TEST_ASSERT_TRUE(logic_payload_valid(MSG_LOGIC_NOT, refs, 3, F));
    TEST_ASSERT_FALSE(logic_payload_valid(MSG_LOGIC_NOT, refs, 6, F));

    /* a delay: a source, then its on and off delays, up to DELAY_MAX_MS */
    uint8_t delay[] = { 0, 1, 0, 0x05, 0x26, 0x5C, 0x00, 0, 0, 0x13, 0x88 };   /* a day, 5 s */
    TEST_ASSERT_TRUE(logic_payload_valid(MSG_LOGIC_DELAY, delay, sizeof delay, F));
    TEST_ASSERT_EQUAL_UINT32(DELAY_MAX_MS, msg_u32_at(delay + 3));
    delay[6] = 0x01;                                                    /* a day and 1 ms */
    TEST_ASSERT_FALSE(logic_payload_valid(MSG_LOGIC_DELAY, delay, sizeof delay, F));
    TEST_ASSERT_FALSE(logic_payload_valid(MSG_HW_LED, delay, 3, F));

    /* a pulse's time is 1 ms or more; a blink's two are too */
    uint8_t times[] = { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1 };
    TEST_ASSERT_FALSE(logic_payload_valid(MSG_LOGIC_PULSE, times, 7, F));
    times[6] = 1;
    TEST_ASSERT_TRUE(logic_payload_valid(MSG_LOGIC_PULSE, times, 7, F));
    TEST_ASSERT_TRUE(logic_payload_valid(MSG_LOGIC_BLINK, times, 11, F));
    times[10] = 0;
    TEST_ASSERT_FALSE(logic_payload_valid(MSG_LOGIC_BLINK, times, 11, F));
    /* a latch: set and reset; a toggle: one */
    TEST_ASSERT_TRUE(logic_payload_valid(MSG_LOGIC_LATCH, refs, 6, F));
    TEST_ASSERT_FALSE(logic_payload_valid(MSG_LOGIC_LATCH, refs, 3, F));
    TEST_ASSERT_TRUE(logic_payload_valid(MSG_LOGIC_TOGGLE, refs, 3, F));
    TEST_ASSERT_TRUE(logic_payload_valid(MSG_LOGIC_MAX, refs, 12, F));
    /* a scale: options, then four finite floats; its input's from and to apart */
    uint8_t scale[] = { 0, 1, 0, SCALE_CLAMP, 0, 0, 0, 0, 0x3F, 0x80, 0, 0, 0, 0, 0, 0, 0x3F, 0x80, 0, 0 };
    TEST_ASSERT_TRUE(logic_payload_valid(MSG_LOGIC_SCALE, scale, sizeof scale, F));
    scale[3] = 0x02;                                                    /* an unknown option */
    TEST_ASSERT_FALSE(logic_payload_valid(MSG_LOGIC_SCALE, scale, sizeof scale, F));
    scale[3] = 0;
    scale[8] = 0; scale[9] = 0;                                         /* from 0 to 0 */
    TEST_ASSERT_FALSE(logic_payload_valid(MSG_LOGIC_SCALE, scale, sizeof scale, F));
    scale[8] = 0x7F; scale[9] = 0xC0;                                   /* to NaN */
    TEST_ASSERT_FALSE(logic_payload_valid(MSG_LOGIC_SCALE, scale, sizeof scale, F));
}

/* Logic runs after the logic it reads, whatever the config's order; a loop still runs. */
void test_program_orders_logic(void)
{
    static const uint8_t cfg[] = {
        MSG_LOGIC_NOT, MSG_FLAG_OUTPUT, 0, 3, 3, 0, 2, 0,       /* NOT n3 <- n2 */
        MSG_LOGIC_NOT, MSG_FLAG_OUTPUT, 0, 2, 3, 0, 1, 0,       /* NOT n2 <- n1 */
        MSG_UI_BUTTON, 0, 0, 1, 0,                              /* button n1 */
        MSG_LOGIC_OR, MSG_FLAG_OUTPUT, 0, 4, 6, 0, 5, 0, 0, 3, 0,   /* OR n4 <- n5, n3 */
        MSG_LOGIC_NOT, MSG_FLAG_OUTPUT, 0, 5, 3, 0, 4, 0,       /* NOT n5 <- n4: a loop */
    };
    static hw_program p;
    program_compile(cfg, sizeof cfg, &p);
    TEST_ASSERT_EQUAL_UINT8(4, p.node_count);
    TEST_ASSERT_EQUAL_UINT16(2, p.nodes[0].id);     /* reads only the button */
    TEST_ASSERT_EQUAL_UINT16(3, p.nodes[1].id);     /* after n2 */
    TEST_ASSERT_EQUAL_UINT16(4, p.nodes[2].id);     /* the loop: in the config's order */
    TEST_ASSERT_EQUAL_UINT16(5, p.nodes[3].id);
    TEST_ASSERT_EQUAL_UINT8(2, p.nodes[2].in_count);
}

/* One logic node, its input signal s0 set to `in`, stepped by `ms`. */
static hw_program step_p;
static prog_logic_state step_st;
static bool step(uint8_t node, float in, bool on, uint32_t *held, uint32_t ms)
{
    step_p.signals[0].value = in;
    step_st.held_ms = *held;
    bool out = program_logic_step(&step_p, &step_p.nodes[node], on, &step_st, ms) >= 0.5f;
    *held = step_st.held_ms;
    return out;
}

void test_program_logic_step(void)
{
    static const uint8_t cfg[] = {
        MSG_UI_SLIDER, 0, 0, 1, 1, 0,                                       /* s0: slider n1 */
        MSG_LOGIC_COMPARE, MSG_FLAG_OUTPUT, 0, 2, 12, 0, 1, 0, COMPARE_ABOVE,
            0x3F, 0x00, 0, 0, 0x3D, 0xCC, 0xCC, 0xCD,                       /* above 0.5, hysteresis 0.1 */
        MSG_LOGIC_COMPARE, MSG_FLAG_OUTPUT, 0, 3, 12, 0, 1, 0, COMPARE_BELOW,
            0x3F, 0x00, 0, 0, 0x3D, 0xCC, 0xCC, 0xCD,                       /* below 0.5, hysteresis 0.1 */
        MSG_LOGIC_AND, MSG_FLAG_OUTPUT, 0, 4, 6, 0, 1, 0, 0, 0, 0,          /* AND n4 <- n1, nothing */
        MSG_LOGIC_OR, MSG_FLAG_OUTPUT, 0, 5, 3, 0, 0, 0,                    /* OR n5 <- nothing */
        MSG_LOGIC_NOT, MSG_FLAG_OUTPUT, 0, 6, 3, 0, 1, 0,                   /* NOT n6 <- n1 */
        MSG_LOGIC_DELAY, MSG_FLAG_OUTPUT, 0, 7, 11, 0, 1, 0,
            0, 0, 0, 100, 0, 0, 0, 0,                                       /* on after 100 ms, off at once */
    };
    program_compile(cfg, sizeof cfg, &step_p);
    TEST_ASSERT_EQUAL_UINT8(6, step_p.node_count);
    uint32_t held = 0;

    /* above 0.5: on past it, off only below 0.4 */
    TEST_ASSERT_FALSE(step(0, 0.5f, false, &held, 10));
    TEST_ASSERT_TRUE(step(0, 0.51f, false, &held, 10));
    TEST_ASSERT_TRUE(step(0, 0.41f, true, &held, 10));
    TEST_ASSERT_FALSE(step(0, 0.39f, true, &held, 10));
    /* below 0.5: on under it, off only above 0.6 */
    TEST_ASSERT_TRUE(step(1, 0.49f, false, &held, 10));
    TEST_ASSERT_TRUE(step(1, 0.59f, true, &held, 10));
    TEST_ASSERT_FALSE(step(1, 0.61f, true, &held, 10));
    /* AND and OR read only their wired inputs; with none, off */
    TEST_ASSERT_TRUE(step(2, 1, false, &held, 10));
    TEST_ASSERT_FALSE(step(2, 0.49f, true, &held, 10));
    TEST_ASSERT_FALSE(step(3, 1, false, &held, 10));
    /* NOT */
    TEST_ASSERT_TRUE(step(4, 0, false, &held, 10));
    TEST_ASSERT_FALSE(step(4, 1, true, &held, 10));
    /* delay: on once the input has been on 100 ms, off at once */
    bool on = false;
    for (int t = 10; t < 100; t += 10) {
        on = step(5, 1, on, &held, 10);
        TEST_ASSERT_FALSE(on);
    }
    on = step(5, 1, on, &held, 10);
    TEST_ASSERT_TRUE(on);
    TEST_ASSERT_EQUAL_UINT32(0, held);
    on = step(5, 0, on, &held, 10);
    TEST_ASSERT_FALSE(on);
    /* a blip shorter than the on delay doesn't turn it on, and starts the count again */
    for (int i = 0; i < 5; i++) on = step(5, 1, on, &held, 10);
    on = step(5, 0, on, &held, 10);
    TEST_ASSERT_FALSE(on);
    TEST_ASSERT_EQUAL_UINT32(0, held);
}

/* The newer logic: a scale, min and max (floating), a pulse, blink, toggle and latch. */
void test_program_logic_more(void)
{
    static const uint8_t cfg[] = {
        MSG_UI_SLIDER, 0, 0, 1, 1, 0,                                       /* s0: slider n1 */
        MSG_UI_SLIDER, 0, 0, 2, 1, 0,                                       /* s1: slider n2 */
        MSG_LOGIC_SCALE, MSG_FLAG_OUTPUT, 0, 3, 20, 0, 1, 0, SCALE_CLAMP,   /* 30-60 to 0-1, clamped */
            0x41, 0xF0, 0, 0,  0x42, 0x70, 0, 0,  0, 0, 0, 0,  0x3F, 0x80, 0, 0,
        MSG_LOGIC_SCALE, MSG_FLAG_OUTPUT, 0, 4, 20, 0, 1, 0, 0,             /* 0-1 to 100-0, not clamped */
            0, 0, 0, 0,  0x3F, 0x80, 0, 0,  0x42, 0xC8, 0, 0,  0, 0, 0, 0,
        MSG_LOGIC_MIN, MSG_FLAG_OUTPUT, 0, 5, 9, 0, 1, 0, 0, 2, 0, 0, 0, 0, /* min n1, n2, nothing */
        MSG_LOGIC_MAX, MSG_FLAG_OUTPUT, 0, 6, 6, 0, 1, 0, 0, 2, 0,          /* max n1, n2 */
        MSG_LOGIC_PULSE, MSG_FLAG_OUTPUT, 0, 7, 7, 0, 1, 0, 0, 0, 0, 50,    /* 50 ms */
        MSG_LOGIC_BLINK, MSG_FLAG_OUTPUT, 0, 8, 11, 0, 1, 0, 0, 0, 0, 30, 0, 0, 0, 20,  /* 30 on, 20 off */
        MSG_LOGIC_TOGGLE, MSG_FLAG_OUTPUT, 0, 9, 3, 0, 1, 0,
        MSG_LOGIC_LATCH, MSG_FLAG_OUTPUT, 0, 10, 6, 0, 1, 0, 0, 2, 0,       /* set n1, reset n2 */
        MSG_LOGIC_BLINK, MSG_FLAG_OUTPUT, 0, 11, 11, 0, 0, 0, 0, 0, 0, 10, 0, 0, 0, 10, /* nothing wired */
    };
    static hw_program p;
    program_compile(cfg, sizeof cfg, &p);
    TEST_ASSERT_EQUAL_UINT8(9, p.node_count);
    config_element_iterator it;
    config_element_iterator_init(&it, cfg, sizeof cfg);
    while (it.next(&it))
        if (program_logic(it.type)) TEST_ASSERT_TRUE(logic_payload_valid(it.type, it.pay, it.len, it.flags));
    TEST_ASSERT_TRUE(program_logic_floating(MSG_LOGIC_SCALE) && program_logic_floating(MSG_LOGIC_MAX)
                     && !program_logic_floating(MSG_LOGIC_LATCH));

    prog_logic_state st = { 0 };
    const prog_node *scale = &p.nodes[0], *flip = &p.nodes[1], *min = &p.nodes[2], *max = &p.nodes[3];
    const prog_node *pulse = &p.nodes[4], *blink = &p.nodes[5], *toggle = &p.nodes[6], *latch = &p.nodes[7];
    const prog_node *always = &p.nodes[8];

    /* scale: 45 °C is half way from 30 to 60; past the range it is clamped */
    p.signals[0].value = 45;
    TEST_ASSERT_EQUAL_FLOAT(0.5f, program_logic_step(&p, scale, 0, &st, 10));
    p.signals[0].value = 75;
    TEST_ASSERT_EQUAL_FLOAT(1.0f, program_logic_step(&p, scale, 0, &st, 10));
    p.signals[0].value = 0.25f;                     /* unclamped and inverted: 75 */
    TEST_ASSERT_EQUAL_FLOAT(75.0f, program_logic_step(&p, flip, 0, &st, 10));
    p.signals[0].value = 2;
    TEST_ASSERT_EQUAL_FLOAT(-100.0f, program_logic_step(&p, flip, 0, &st, 10));

    /* min and max of the wired inputs */
    p.signals[0].value = 0.25f;
    p.signals[1].value = 0.75f;
    TEST_ASSERT_EQUAL_FLOAT(0.25f, program_logic_step(&p, min, 0, &st, 10));
    TEST_ASSERT_EQUAL_FLOAT(0.75f, program_logic_step(&p, max, 0, &st, 10));

    /* pulse: on for 50 ms from the input going on; it doesn't wait for the input */
    float out = 0;
    p.signals[0].value = 0;
    program_logic_start(&p, pulse, &st);
    p.signals[0].value = 1;
    out = program_logic_step(&p, pulse, out, &st, 10);
    TEST_ASSERT_EQUAL_FLOAT(1, out);
    for (int t = 10; t < 50; t += 10) {
        out = program_logic_step(&p, pulse, out, &st, 10);
        TEST_ASSERT_EQUAL_FLOAT(1, out);
    }
    out = program_logic_step(&p, pulse, out, &st, 10);
    TEST_ASSERT_EQUAL_FLOAT(0, out);                /* 50 ms, though the input is still on */
    out = program_logic_step(&p, pulse, out, &st, 10);
    TEST_ASSERT_EQUAL_FLOAT(0, out);
    /* an input already on when the program starts doesn't count as going on */
    program_logic_start(&p, pulse, &st);
    TEST_ASSERT_EQUAL_FLOAT(0, program_logic_step(&p, pulse, 0, &st, 10));

    /* blink while the input is on: 30 ms on, 20 ms off, from on */
    program_logic_start(&p, blink, &st);
    out = 0;
    char seq[11] = { 0 };
    for (int i = 0; i < 10; i++) {
        out = program_logic_step(&p, blink, out, &st, 10);
        seq[i] = out >= 0.5f ? '1' : '0';
    }
    TEST_ASSERT_EQUAL_STRING("1110011100", seq);
    p.signals[0].value = 0;
    TEST_ASSERT_EQUAL_FLOAT(0, program_logic_step(&p, blink, 1, &st, 10));
    /* with nothing wired, it always blinks */
    program_logic_start(&p, always, &st);
    TEST_ASSERT_EQUAL_FLOAT(1, program_logic_step(&p, always, 0, &st, 10));
    TEST_ASSERT_EQUAL_FLOAT(0, program_logic_step(&p, always, 1, &st, 10));

    /* toggle: turns over each time its input goes on */
    p.signals[0].value = 0;
    program_logic_start(&p, toggle, &st);
    out = 0;
    const float presses[] = { 1, 1, 0, 0, 1, 0, 1 };
    const float toggled[] = { 1, 1, 1, 1, 0, 0, 1 };
    for (int i = 0; i < 7; i++) {
        p.signals[0].value = presses[i];
        out = program_logic_step(&p, toggle, out, &st, 10);
        TEST_ASSERT_EQUAL_FLOAT(toggled[i], out);
    }

    /* latch: set turns it on and it stays on; reset turns it off, and wins */
    p.signals[0].value = 1; p.signals[1].value = 0;
    out = program_logic_step(&p, latch, 0, &st, 10);
    TEST_ASSERT_EQUAL_FLOAT(1, out);
    p.signals[0].value = 0;
    out = program_logic_step(&p, latch, out, &st, 10);
    TEST_ASSERT_EQUAL_FLOAT(1, out);
    p.signals[0].value = 1; p.signals[1].value = 1;
    out = program_logic_step(&p, latch, out, &st, 10);
    TEST_ASSERT_EQUAL_FLOAT(0, out);
}

/* A PWM takes a scale's output as a fraction of 1, and a max's in the unit of what it picks from. */
void test_program_pwm_full_scale_through_logic(void)
{
    static const uint8_t cfg[] = {
        MSG_HW_ADC, 0, 0, 1, 1, 0,                                          /* ADC n1 */
        MSG_LOGIC_MAX, MSG_FLAG_OUTPUT, 0, 2, 3, 0, 1, 0,                   /* max n2 <- n1 */
        MSG_LOGIC_SCALE, MSG_FLAG_OUTPUT, 0, 3, 20, 0, 1, 0, 0,             /* scale n3 <- n1 */
            0, 0, 0, 0,  0x40, 0x53, 0x33, 0x33,  0, 0, 0, 0,  0x3F, 0x80, 0, 0,
        MSG_HW_PWM, MSG_FLAG_OUTPUT, 0, 4, 17, 0, 2, 0, 2, 0x03, 0xE8, 0x03,    /* PWM n4: duty <- n2, */
            50, 0, 100, 0, 3, 0, 0, 100, 0x27, 0x10,                            /*   frequency <- n3 */
    };
    static hw_program p;
    program_compile(cfg, sizeof cfg, &p);
    const prog_node *pwm = &p.nodes[p.node_count - 1];
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_PWM, pwm->type);
    TEST_ASSERT_EQUAL_FLOAT(PROG_ADC_VOLTS, pwm->in_full[0]);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, pwm->in_full[1]);
}

/* ── Steppers ─────────────────────────────────────────────────────────── */

/* A stepper on GPIO 10-13, wired from n1: half-stepped, 2048 full steps, 1 revolution
   per unit, 15 RPM, 30 RPM/s. `options` replaces its options. */
static void stepper_config(uint8_t cfg[33], uint8_t options)
{
    const uint8_t c[33] = {
        MSG_UI_SLIDER, 0, 0, 1, 1, 0,                                   /* slider n1 */
        MSG_HW_STEPPER, MSG_FLAG_OUTPUT, 0, 2, 22, 0, 1, 0,             /* stepper n2 <- n1 */
            10, 11, 12, 13, options, 0x08, 0x00,                        /*   pins, options, 2048 steps */
            0x3F, 0x80, 0, 0,  0x41, 0x70, 0, 0,  0x41, 0xF0, 0, 0,     /*   1, 15 RPM, 30 RPM/s */
    };
    memcpy(cfg, c, sizeof c);
}

void test_stepper_payload_read(void)
{
    uint8_t cfg[33];
    stepper_config(cfg, STEPPER_HALF);
    const uint8_t *pay = cfg + 6 + MSG_HEADER_LEN;
    stepper_payload sp;
    TEST_ASSERT_TRUE(stepper_payload_read(pay, 22, MSG_FLAG_OUTPUT, &sp));
    TEST_ASSERT_EQUAL_UINT16(1, sp.source.id);
    TEST_ASSERT_EQUAL_UINT8(13, sp.pins[3]);
    TEST_ASSERT_EQUAL_HEX8(STEPPER_HALF, sp.options);
    TEST_ASSERT_EQUAL_UINT16(2048, sp.steps_rev);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, sp.scale);
    TEST_ASSERT_EQUAL_FLOAT(15.0f, sp.rpm);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, sp.accel);
    TEST_ASSERT_FALSE(stepper_payload_read(pay, 21, MSG_FLAG_OUTPUT, &sp));    /* too short */
    uint8_t legacy[21] = { 0, 1 };                                              /* a 2-byte reference */
    memcpy(legacy + 2, pay + 3, 19);
    TEST_ASSERT_TRUE(stepper_payload_read(legacy, 21, 0, &sp));
    TEST_ASSERT_EQUAL_UINT16(1, sp.source.id);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, sp.accel);

    /* Each of these makes it one the device doesn't run: 1 or 2 bytes from `at`. */
    static const struct { uint8_t at, n, b[2]; } bad[] = {
        { 3, 1, { 23 } },               /* IN1 on GPIO 23, the WiFi chip's */
        { 4, 1, { 10 } },               /* IN2 on IN1's pin */
        { 7, 1, { 0x03 } },             /* no such drive */
        { 7, 1, { 0x22 } },             /* an unknown option */
        { 8, 2, { 0, 3 } },             /* 3 steps a revolution */
        { 14, 2, { 0, 0 } },            /* top speed 0 */
        { 14, 1, { 0xC1 } },            /* top speed below 0 */
        { 14, 1, { 0x44 } },            /* top speed 960 RPM, over STEPPER_RPM_MAX */
        { 18, 1, { 0xC1 } },            /* acceleration below 0 */
        { 10, 2, { 0x7F, 0xC0 } },      /* a scale that is NaN */
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        uint8_t b[22];
        memcpy(b, pay, sizeof b);
        memcpy(b + bad[i].at, bad[i].b, bad[i].n);
        TEST_ASSERT_FALSE_MESSAGE(stepper_payload_read(b, 22, MSG_FLAG_OUTPUT, &sp), "a bad stepper payload was read");
    }
}

/* The program's stepper node, from stepper_config. */
static const prog_node *compile_stepper(hw_program *p, uint8_t options)
{
    uint8_t cfg[33];
    stepper_config(cfg, options);
    program_compile(cfg, sizeof cfg, p);
    TEST_ASSERT_EQUAL_UINT8(1, p->node_count);
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_STEPPER, p->nodes[0].type);
    return &p->nodes[0];
}

void test_stepper_motion_of(void)
{
    static hw_program p;
    const prog_node *n = compile_stepper(&p, STEPPER_HALF);
    TEST_ASSERT_EQUAL_UINT8(program_signal(&p, 1, 0), n->in[0]);
    stepper_motion m = stepper_motion_of(n);
    TEST_ASSERT_EQUAL_FLOAT(4096, m.steps_rev);             /* half steps: twice 2048 */
    TEST_ASSERT_EQUAL_FLOAT(1024, m.rate_max);              /* 15 RPM × 4096 / 60 */
    TEST_ASSERT_EQUAL_FLOAT(2048, m.accel);                 /* 30 RPM/s × 4096 / 60 */
    TEST_ASSERT_EQUAL_INT32(1024, stepper_target(n, &m, 0.25f));
    TEST_ASSERT_EQUAL_INT32(-4096, stepper_target(n, &m, -1));

    n = compile_stepper(&p, STEPPER_FULL);
    m = stepper_motion_of(n);
    TEST_ASSERT_EQUAL_FLOAT(2048, m.steps_rev);
    TEST_ASSERT_EQUAL_FLOAT(512, m.rate_max);

    /* A top speed past STEPPER_RATE_MAX steps a second is held there. */
    p.nodes[0].rpm = 300;
    m = stepper_motion_of(&p.nodes[0]);
    TEST_ASSERT_EQUAL_FLOAT(STEPPER_RATE_MAX, m.rate_max);
}

/* Run a stepper for up to `ms` milliseconds in steps of 0.1 ms, from where it is, with
   its input at `in`. Returns the time it took to settle, in ms; -1 if it didn't.
   Checks that it never steps faster than its top speed allows, never jumps more
   than a step, and never passes its target. */
static float run_stepper(const prog_node *n, const stepper_motion *m, stepper_state *st, float in, float ms)
{
    const float dt = 0.0001f;
    int32_t target = stepper_target(n, m, in), from = st->pos;
    st->moving = !stepper_settled(n, m, st, in);
    float since = 1e9f;          /* seconds since its last step */
    for (float t = 0; t < ms / 1000; t += dt) {
        if (!st->moving) return t * 1000;
        since += dt;
        int32_t was = st->pos;
        int step = stepper_advance(n, m, st, in, dt);
        TEST_ASSERT_EQUAL_INT32(was + step, st->pos);
        TEST_ASSERT_TRUE(fabsf(st->speed) <= m->rate_max * 1.0001f);
        if (step) {
            TEST_ASSERT_TRUE_MESSAGE(since >= 1 / m->rate_max - 2 * dt, "stepped faster than its top speed");
            since = 0;
        }
        if (!(n->options & STEPPER_SPEED)) {
            if (target >= from) TEST_ASSERT_TRUE_MESSAGE(st->pos <= target, "went past its target");
            else TEST_ASSERT_TRUE_MESSAGE(st->pos >= target, "went past its target");
        }
    }
    return st->moving ? -1 : ms;
}

void test_stepper_moves_to_its_target(void)
{
    static hw_program p;
    const prog_node *n = compile_stepper(&p, STEPPER_HALF);
    stepper_motion m = stepper_motion_of(n);
    stepper_state st = { 0 };

    /* Still at 0 with its input at 0. */
    TEST_ASSERT_TRUE(stepper_settled(n, &m, &st, 0));
    TEST_ASSERT_FALSE(stepper_settled(n, &m, &st, 0.001f));

    /* A whole turn, 4096 steps: half a second up to 1024 steps a second and half a
       second down, 512 steps, so 3584 at the top speed, 3.5 s: 4.5 s in all. */
    float ms = run_stepper(n, &m, &st, 1, 6000);
    TEST_ASSERT_EQUAL_INT32(4096, st.pos);
    TEST_ASSERT_EQUAL_FLOAT(0, st.speed);
    TEST_ASSERT_FLOAT_WITHIN(30, 4500, ms);

    /* Back a quarter turn. */
    run_stepper(n, &m, &st, 0.75f, 3000);
    TEST_ASSERT_EQUAL_INT32(3072, st.pos);

    /* Without an acceleration, at its top speed from the first step: 1024 steps in 1 s. */
    p.nodes[0].accel = 0;
    m = stepper_motion_of(n);
    ms = run_stepper(n, &m, &st, 1, 3000);
    TEST_ASSERT_EQUAL_INT32(4096, st.pos);
    TEST_ASSERT_FLOAT_WITHIN(5, 1000, ms);
}

void test_stepper_turns_at_a_speed(void)
{
    static hw_program p;
    const prog_node *n = compile_stepper(&p, STEPPER_HALF | STEPPER_SPEED);
    p.nodes[0].turn_scale = 10;         /* 10 RPM per unit */
    stepper_motion m = stepper_motion_of(n);
    stepper_state st = { 0 };

    /* 0.5 → 5 RPM, 341.3 steps a second; reached within 5 / 30 s. */
    TEST_ASSERT_EQUAL_FLOAT(-1, run_stepper(n, &m, &st, 0.5f, 1000));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5 * 4096.0f / 60, st.speed);
    int32_t at = st.pos;
    run_stepper(n, &m, &st, 0.5f, 1000);
    TEST_ASSERT_INT32_WITHIN(2, 341, st.pos - at);

    /* Its input past the top speed: 15 RPM. Backwards below 0. */
    run_stepper(n, &m, &st, 3, 1000);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, m.rate_max, st.speed);
    run_stepper(n, &m, &st, -0.5f, 2000);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -5 * 4096.0f / 60, st.speed);

    /* 0 stops it, within 5 / 30 s, and then it is settled. */
    float ms = run_stepper(n, &m, &st, 0, 1000);
    TEST_ASSERT_FLOAT_WITHIN(10, 1000.0f * 5 / 30, ms);
    TEST_ASSERT_EQUAL_FLOAT(0, st.speed);
    TEST_ASSERT_TRUE(stepper_settled(n, &m, &st, 0));
}

void test_stepper_coils(void)
{
    static hw_program p;
    static const uint8_t half[8] = { 0x1, 0x3, 0x2, 0x6, 0x4, 0xC, 0x8, 0x9 };
    static const uint8_t full[4] = { 0x3, 0x6, 0xC, 0x9 };
    static const uint8_t wave[4] = { 0x1, 0x2, 0x4, 0x8 };
    const prog_node *n = compile_stepper(&p, STEPPER_HALF);
    for (int32_t pos = -16; pos < 16; pos++) TEST_ASSERT_EQUAL_HEX8(half[pos & 7], stepper_coils(n, pos));
    n = compile_stepper(&p, STEPPER_FULL);
    for (int32_t pos = -8; pos < 8; pos++) TEST_ASSERT_EQUAL_HEX8(full[pos & 3], stepper_coils(n, pos));
    n = compile_stepper(&p, STEPPER_WAVE);
    for (int32_t pos = -8; pos < 8; pos++) TEST_ASSERT_EQUAL_HEX8(wave[pos & 3], stepper_coils(n, pos));
    /* Reversed, it runs the cycle the other way. */
    n = compile_stepper(&p, STEPPER_FULL | STEPPER_REVERSE);
    for (int32_t pos = -8; pos < 8; pos++) TEST_ASSERT_EQUAL_HEX8(full[-pos & 3], stepper_coils(n, pos));
}

/* ── Servos ───────────────────────────────────────────────────────────── */

/* An ADC n1 driving a servo n2 on GPIO 14: 0° to 180°, 500 to 2400 µs, at most `speed` °/s. */
static const prog_node *compile_servo(hw_program *p, uint8_t speed_hi, uint8_t speed_lo)
{
    const uint8_t cfg[] = {
        MSG_HW_ADC, 0, 0, 1, 1, 0,                                      /* ADC n1 */
        MSG_HW_SERVO, MSG_FLAG_OUTPUT, 0, 2, 12, 0, 1, 0,               /* servo n2 <- n1 */
            14, 0, 180, 0x01, 0xF4, 0x09, 0x60, speed_hi, speed_lo,     /*   pin, angles, pulses, speed */
    };
    program_compile(cfg, sizeof cfg, p);
    TEST_ASSERT_EQUAL_UINT8(2, p->node_count);
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_SERVO, p->nodes[1].type);
    return &p->nodes[1];
}

void test_servo_payload_read(void)
{
    static const uint8_t pay[12] = { 0, 1, 0, 14, 0, 180, 0x01, 0xF4, 0x09, 0x60, 0, 90 };
    servo_payload sv;
    TEST_ASSERT_TRUE(servo_payload_read(pay, 12, MSG_FLAG_OUTPUT, &sv));
    TEST_ASSERT_EQUAL_UINT16(1, sv.source.id);
    TEST_ASSERT_EQUAL_UINT8(14, sv.pin);
    TEST_ASSERT_EQUAL_UINT8(180, sv.angle_max);
    TEST_ASSERT_EQUAL_UINT16(500, sv.pulse_min);
    TEST_ASSERT_EQUAL_UINT16(2400, sv.pulse_max);
    TEST_ASSERT_EQUAL_UINT16(90, sv.speed);
    TEST_ASSERT_TRUE(servo_payload_read(pay + 1, 11, 0, &sv));      /* a 2-byte reference: ID 0x0100 */
    TEST_ASSERT_EQUAL_UINT8(14, sv.pin);
    TEST_ASSERT_FALSE(servo_payload_read(pay, 11, MSG_FLAG_OUTPUT, &sv));

    /* Each of these makes it one the device doesn't run: 1 or 2 bytes from `at`. */
    static const struct { uint8_t at, n, b[2]; } bad[] = {
        { 3, 1, { 25 } },               /* GPIO 25, the WiFi chip's */
        { 4, 1, { 181 } },              /* an angle past 180° */
        { 5, 1, { 200 } },
        { 6, 2, { 0, 99 } },            /* a pulse under SERVO_PULSE_MIN */
        { 8, 2, { 0x0B, 0xB9 } },       /* a pulse over SERVO_PULSE_MAX: 3001 µs */
        { 10, 2, { 0x0E, 0x11 } },      /* a speed over SERVO_SPEED_MAX: 3601 °/s */
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        uint8_t b[12];
        memcpy(b, pay, sizeof b);
        memcpy(b + bad[i].at, bad[i].b, bad[i].n);
        TEST_ASSERT_FALSE_MESSAGE(servo_payload_read(b, 12, MSG_FLAG_OUTPUT, &sv), "a bad servo payload was read");
    }
}

void test_servo_angle_and_pulse(void)
{
    static hw_program p;
    const prog_node *n = compile_servo(&p, 0, 0);
    /* An ADC is taken as a fraction of 3.3 V, clamped. */
    TEST_ASSERT_EQUAL_FLOAT(PROG_ADC_VOLTS, n->in_full[0]);
    TEST_ASSERT_EQUAL_FLOAT(0, servo_target(n, 0));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 90, servo_target(n, 1.65f));
    TEST_ASSERT_EQUAL_FLOAT(180, servo_target(n, 5));
    TEST_ASSERT_EQUAL_FLOAT(0, servo_target(n, -1));
    TEST_ASSERT_EQUAL_FLOAT(500, servo_pulse_us(n, 0));
    TEST_ASSERT_EQUAL_FLOAT(1450, servo_pulse_us(n, 90));
    TEST_ASSERT_EQUAL_FLOAT(2400, servo_pulse_us(n, 180));
    /* No speed limit: straight there. */
    TEST_ASSERT_EQUAL_FLOAT(170, servo_move(n, 10, 170, 0.01f));

    /* Angles may run downwards: 150° at 0, 30° at full scale. */
    p.nodes[1].angle_min = 150;
    p.nodes[1].angle_max = 30;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 90, servo_target(n, 1.65f));
    TEST_ASSERT_EQUAL_FLOAT(30, servo_target(n, 3.3f));

    /* 90 °/s: 0.9° a scan, either way, and no further than the target. */
    n = compile_servo(&p, 0, 90);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 10.9f, servo_move(n, 10, 170, 0.01f));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 9.1f, servo_move(n, 10, 0, 0.01f));
    TEST_ASSERT_EQUAL_FLOAT(10.5f, servo_move(n, 10, 10.5f, 0.01f));
    float angle = 0;
    int scans = 0;
    while (angle != 180 && scans < 1000) { angle = servo_move(n, angle, 180, 0.01f); scans++; }
    TEST_ASSERT_INT_WITHIN(1, 200, scans);          /* 180° at 90 °/s: 2 s */
}

/* ── NeoPixels ────────────────────────────────────────────────────────── */

/* A NeoPixel payload with outputs: source n1, no on/off source, GPIO 16, a layout,
   options and brightness 255, its size, then a graphic: its type, then `glen` bytes of
   settings. Returns its length. */
static uint8_t neopixel_pay(uint8_t *pay, uint8_t layout, uint8_t options, const uint8_t *size, uint8_t k,
                            uint8_t graphic, const uint8_t *set, uint8_t glen)
{
    uint8_t n = 0;
    const uint8_t head[] = { 0, 1, 0, 0, 0, 0, 16, layout, options, 255, k };
    memcpy(pay, head, sizeof head);
    n += sizeof head;
    memcpy(pay + n, size, k);
    n += k;
    pay[n++] = graphic;
    pay[n++] = glen;
    memcpy(pay + n, set, glen);
    return (uint8_t)(n + glen);
}

/* A bar along `along` at `position`, with options, coloured red 10, green 20, blue 30. */
static uint8_t bar_pay(uint8_t *pay, uint8_t layout, uint8_t options, const uint8_t *size, uint8_t k,
                       uint8_t along, uint8_t position, uint8_t gfx_options)
{
    const uint8_t set[] = { along, position, gfx_options, 10, 20, 30 };
    return neopixel_pay(pay, layout, options, size, k, NEOPIXEL_GFX_BAR, set, sizeof set);
}

static int bar_ok(uint8_t layout, uint8_t options, const uint8_t *size, uint8_t k,
                  uint8_t along, uint8_t position, uint8_t gfx_options)
{
    uint8_t pay[64];
    neopixel_payload px;
    uint8_t len = bar_pay(pay, layout, options, size, k, along, position, gfx_options);
    return neopixel_payload_read(pay, len, MSG_FLAG_OUTPUT, &px);
}

/* A point from (a0, b0) to (a1, b1), with options, coloured red 10, green 20, blue 30. */
static int point_ok(uint8_t layout, const uint8_t *size, uint8_t k, uint8_t a0, uint8_t b0, uint8_t a1, uint8_t b1)
{
    uint8_t pay[64];
    neopixel_payload px;
    const uint8_t set[] = { a0, b0, a1, b1, 0, 10, 20, 30 };
    uint8_t len = neopixel_pay(pay, layout, 0, size, k, NEOPIXEL_GFX_POINT, set, sizeof set);
    return neopixel_payload_read(pay, len, MSG_FLAG_OUTPUT, &px);
}

void test_neopixel_payload_read(void)
{
    static const uint8_t grid[] = { 8, 8 }, rings[] = { 24, 16, 12, 8, 1 }, ring[] = { 24 };
    uint8_t pay[64];
    neopixel_payload px;
    uint8_t len = bar_pay(pay, NEOPIXEL_ARRAY, NEOPIXEL_ZIGZAG, grid, 2, NEOPIXEL_BAR_COLUMN, 7, NEOPIXEL_GFX_REVERSE);
    TEST_ASSERT_EQUAL_UINT8(21, len);
    pay[3] = 0; pay[4] = 3;                                                 /* on/off from n3 */
    TEST_ASSERT_TRUE(neopixel_payload_read(pay, len, MSG_FLAG_OUTPUT, &px));
    TEST_ASSERT_EQUAL_UINT16(1, px.source.id);
    TEST_ASSERT_EQUAL_UINT16(3, px.enable.id);
    TEST_ASSERT_EQUAL_UINT8(16, px.pin);
    TEST_ASSERT_EQUAL_UINT16(64, px.leds);
    TEST_ASSERT_EQUAL_UINT8(NEOPIXEL_BAR_COLUMN, px.along);
    TEST_ASSERT_EQUAL_UINT8(7, px.position);
    TEST_ASSERT_EQUAL_UINT8(NEOPIXEL_GFX_REVERSE, px.gfx_options);
    TEST_ASSERT_EQUAL_UINT8(30, px.colour[0][2]);
    TEST_ASSERT_EQUAL_UINT8(30, px.colour[1][2]);                           /* one colour: the same at 1 */
    uint8_t two[64];                                                        /* 2-byte references: IDs 0x0100 and 0x0300 */
    memcpy(two, pay + 1, 2);
    memcpy(two + 2, pay + 4, len - 4);
    TEST_ASSERT_TRUE(neopixel_payload_read(two, len - 2, 0, &px));
    TEST_ASSERT_EQUAL_UINT16(0x0300, px.enable.id);
    TEST_ASSERT_EQUAL_UINT8(16, px.pin);
    TEST_ASSERT_FALSE(neopixel_payload_read(pay, len - 1, MSG_FLAG_OUTPUT, &px));
    TEST_ASSERT_FALSE(neopixel_payload_read(pay, 4, MSG_FLAG_OUTPUT, &px));

    /* A colour from its input: a second colour, at 1. */
    const uint8_t two_colours[] = { NEOPIXEL_BAR_ROW, 0, NEOPIXEL_GFX_COLOUR_INPUT, 0, 255, 0, 255, 0, 0 };
    len = neopixel_pay(pay, NEOPIXEL_ARRAY, 0, grid, 2, NEOPIXEL_GFX_BAR, two_colours, sizeof two_colours);
    TEST_ASSERT_TRUE(neopixel_payload_read(pay, len, MSG_FLAG_OUTPUT, &px));
    TEST_ASSERT_EQUAL_UINT8(255, px.colour[0][1]);
    TEST_ASSERT_EQUAL_UINT8(255, px.colour[1][0]);
    len = neopixel_pay(pay, NEOPIXEL_ARRAY, 0, grid, 2, NEOPIXEL_GFX_BAR, two_colours, 6);  /* the flag, but one colour */
    TEST_ASSERT_FALSE(neopixel_payload_read(pay, len, MSG_FLAG_OUTPUT, &px));

    TEST_ASSERT_TRUE(bar_ok(NEOPIXEL_CIRCLE, 0, rings, 5, NEOPIXEL_BAR_ROW, 4, 0));
    TEST_ASSERT_TRUE(bar_ok(NEOPIXEL_CIRCLE, 0, rings, 5, NEOPIXEL_BAR_COLUMN, 0, NEOPIXEL_GFX_REVERSE));
    TEST_ASSERT_TRUE(bar_ok(NEOPIXEL_DOUGHNUT, 0, ring, 1, NEOPIXEL_BAR_ROW, 23, NEOPIXEL_GFX_REVERSE));
    static const uint8_t strip[] = { 1, 255 }, big[] = { 16, 16 }, over[] = { 16, 17 };
    TEST_ASSERT_TRUE(bar_ok(NEOPIXEL_ARRAY, 0, strip, 2, NEOPIXEL_BAR_ROW, 0, 0));
    TEST_ASSERT_TRUE(bar_ok(NEOPIXEL_ARRAY, 0, big, 2, NEOPIXEL_BAR_ROW, 15, 0));

    /* Each of these is one the device doesn't run. */
    static const uint8_t zero[] = { 8, 0 }, rings9[] = { 1, 1, 1, 1, 1, 1, 1, 1, 1 }, rings_big[] = { 200, 57 };
    TEST_ASSERT_FALSE(bar_ok(NEOPIXEL_ARRAY, 0, over, 2, NEOPIXEL_BAR_ROW, 0, 0));          /* 272 LEDs */
    TEST_ASSERT_FALSE(bar_ok(NEOPIXEL_ARRAY, 0, zero, 2, NEOPIXEL_BAR_ROW, 0, 0));          /* no columns */
    TEST_ASSERT_FALSE(bar_ok(NEOPIXEL_ARRAY, 0, ring, 1, NEOPIXEL_BAR_ROW, 0, 0));          /* no columns given */
    TEST_ASSERT_FALSE(bar_ok(NEOPIXEL_ARRAY, 0, grid, 2, NEOPIXEL_BAR_ROW, 8, 0));          /* row 9 of 8 */
    TEST_ASSERT_FALSE(bar_ok(NEOPIXEL_ARRAY, 0, strip, 2, NEOPIXEL_BAR_COLUMN, 255, 0));    /* column 256 of 255 */
    TEST_ASSERT_FALSE(bar_ok(NEOPIXEL_ARRAY, 0x02, grid, 2, NEOPIXEL_BAR_ROW, 0, 0));       /* an unknown option */
    TEST_ASSERT_FALSE(bar_ok(NEOPIXEL_ARRAY, 0, grid, 2, 2, 0, 0));                         /* along neither */
    TEST_ASSERT_FALSE(bar_ok(NEOPIXEL_ARRAY, 0, grid, 2, NEOPIXEL_BAR_ROW, 0, 0x04));       /* an unknown graphic option */
    TEST_ASSERT_FALSE(bar_ok(NEOPIXEL_CIRCLE, 0, rings9, 9, NEOPIXEL_BAR_ROW, 0, 0));       /* 9 rings */
    TEST_ASSERT_FALSE(bar_ok(NEOPIXEL_CIRCLE, 0, rings_big, 2, NEOPIXEL_BAR_ROW, 0, 0));    /* 257 LEDs */
    TEST_ASSERT_FALSE(bar_ok(NEOPIXEL_CIRCLE, 0, rings, 5, NEOPIXEL_BAR_ROW, 5, 0));        /* ring 6 of 5 */
    TEST_ASSERT_FALSE(bar_ok(NEOPIXEL_CIRCLE, 0, rings, 5, NEOPIXEL_BAR_COLUMN, 1, 0));     /* across, from ring 2 */
    TEST_ASSERT_FALSE(bar_ok(NEOPIXEL_DOUGHNUT, 0, grid, 2, NEOPIXEL_BAR_ROW, 0, 0));       /* two sizes */
    TEST_ASSERT_FALSE(bar_ok(NEOPIXEL_DOUGHNUT, 0, ring, 1, NEOPIXEL_BAR_COLUMN, 0, 0));
    TEST_ASSERT_FALSE(bar_ok(NEOPIXEL_DOUGHNUT, 0, ring, 1, NEOPIXEL_BAR_ROW, 24, 0));      /* LED 25 of 24 */
    TEST_ASSERT_FALSE(bar_ok(3, 0, ring, 1, NEOPIXEL_BAR_ROW, 0, 0));                       /* no such layout */

    /* A point: from one place the layout has to another. */
    TEST_ASSERT_TRUE(point_ok(NEOPIXEL_ARRAY, grid, 2, 0, 0, 7, 7));
    TEST_ASSERT_TRUE(point_ok(NEOPIXEL_CIRCLE, rings, 5, 0, 23, 4, 0));
    TEST_ASSERT_TRUE(point_ok(NEOPIXEL_DOUGHNUT, ring, 1, 0, 3, 0, 3));
    {
        /* A patch takes the same places; not one off its LEDs. */
        const uint8_t ok[] = { 0, 0, 7, 7, 0, 10, 20, 30 }, off[] = { 0, 0, 8, 7, 0, 10, 20, 30 };
        len = neopixel_pay(pay, NEOPIXEL_ARRAY, 0, grid, 2, NEOPIXEL_GFX_PATCH, ok, sizeof ok);
        TEST_ASSERT_TRUE(neopixel_payload_read(pay, len, MSG_FLAG_OUTPUT, &px));
        TEST_ASSERT_EQUAL_UINT8(7, px.to[1]);
        len = neopixel_pay(pay, NEOPIXEL_ARRAY, 0, grid, 2, NEOPIXEL_GFX_PATCH, off, sizeof off);
        TEST_ASSERT_FALSE(neopixel_payload_read(pay, len, MSG_FLAG_OUTPUT, &px));
    }
    TEST_ASSERT_FALSE(point_ok(NEOPIXEL_ARRAY, grid, 2, 0, 0, 8, 0));        /* row 9 of 8 */
    TEST_ASSERT_FALSE(point_ok(NEOPIXEL_ARRAY, grid, 2, 0, 8, 0, 0));
    TEST_ASSERT_FALSE(point_ok(NEOPIXEL_CIRCLE, rings, 5, 0, 0, 5, 0));      /* ring 6 of 5 */
    TEST_ASSERT_FALSE(point_ok(NEOPIXEL_CIRCLE, rings, 5, 4, 1, 0, 0));      /* LED 2 of the ring of 1 */
    TEST_ASSERT_FALSE(point_ok(NEOPIXEL_DOUGHNUT, ring, 1, 1, 0, 0, 0));     /* a doughnut has ring 0 only */
    TEST_ASSERT_FALSE(point_ok(NEOPIXEL_DOUGHNUT, ring, 1, 0, 0, 0, 24));

    len = bar_pay(pay, NEOPIXEL_DOUGHNUT, 0, ring, 1, NEOPIXEL_BAR_ROW, 0, 0);
    pay[6] = 25;                                                            /* the WiFi chip's GPIO */
    TEST_ASSERT_FALSE(neopixel_payload_read(pay, len, MSG_FLAG_OUTPUT, &px));
    pay[6] = 16;
    pay[len - 8] = 3;                                                       /* no such graphic */
    TEST_ASSERT_FALSE(neopixel_payload_read(pay, len, MSG_FLAG_OUTPUT, &px));
}

/* A NeoPixel node with these settings, compiled behind an ADC n1, a button n3 on its
   on/off input. */
static const prog_node *compile_neopixel_gfx(hw_program *p, uint8_t layout, uint8_t options, const uint8_t *size,
                                             uint8_t k, uint8_t graphic, const uint8_t *set, uint8_t glen)
{
    uint8_t cfg[96] = { MSG_HW_ADC, 0, 0, 1, 1, 0, MSG_UI_BUTTON, 0, 0, 3, 0, MSG_HW_NEOPIXEL, MSG_FLAG_OUTPUT, 0, 2 };
    cfg[15] = neopixel_pay(cfg + 16, layout, options, size, k, graphic, set, glen);
    cfg[16 + 4] = 3;                        /* on/off <- n3 */
    program_compile(cfg, 16u + cfg[15], p);
    TEST_ASSERT_EQUAL_UINT8(2, p->node_count);
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_NEOPIXEL, p->nodes[1].type);
    return &p->nodes[1];
}

static const prog_node *compile_neopixel(hw_program *p, uint8_t layout, uint8_t options, const uint8_t *size, uint8_t k,
                                         uint8_t along, uint8_t position, uint8_t gfx_options)
{
    const uint8_t set[] = { along, position, gfx_options, 10, 20, 30 };
    return compile_neopixel_gfx(p, layout, options, size, k, NEOPIXEL_GFX_BAR, set, sizeof set);
}

static const prog_node *compile_point(hw_program *p, uint8_t layout, const uint8_t *size, uint8_t k,
                                      uint8_t a0, uint8_t b0, uint8_t a1, uint8_t b1, uint8_t gfx_options)
{
    const uint8_t set[] = { a0, b0, a1, b1, gfx_options, 10, 20, 30 };
    return compile_neopixel_gfx(p, layout, 0, size, k, NEOPIXEL_GFX_POINT, set, sizeof set);
}

/* The colours as a WS2812 takes them, red 10, green 20, blue 30 (neopixel_pay's) at `k`. */
#define LIT(k) ((uint32_t)(20 * (k) + 0.5f) << 24 | (uint32_t)(10 * (k) + 0.5f) << 16 | (uint32_t)(30 * (k) + 0.5f) << 8)

/* The LEDs out of `leds` that are lit, as a string of their numbers, one each: "0 1 2". */
static const char *lit_leds(const uint32_t *out, uint16_t leds)
{
    static char text[1024];
    size_t n = 0;
    text[0] = 0;
    for (uint16_t i = 0; i < leds; i++)
        if (out[i]) n += (size_t)snprintf(text + n, sizeof text - n, "%s%u", n ? " " : "", i);
    return text;
}

void test_neopixel_bar(void)
{
    static hw_program p;
    static uint32_t out[NEOPIXEL_LEDS_MAX];
    static const uint8_t grid[] = { 4, 5 };     /* 4 rows of 5 */

    /* Along row 2 of a 4 × 5 array, from the left: an ADC's 1.65 V is half way, 2.5 LEDs. */
    const prog_node *n = compile_neopixel(&p, NEOPIXEL_ARRAY, 0, grid, 2, NEOPIXEL_BAR_ROW, 1, 0);
    TEST_ASSERT_EQUAL_FLOAT(PROG_ADC_VOLTS, n->in_full[0]);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, neopixel_fraction(n, 1.65f));
    TEST_ASSERT_EQUAL_FLOAT(1, neopixel_fraction(n, 9));
    TEST_ASSERT_EQUAL_FLOAT(0, neopixel_fraction(n, -1));
    neopixel_render(&n->px, 0.5f, true, out);
    TEST_ASSERT_EQUAL_STRING("5 6 7", lit_leds(out, 20));
    TEST_ASSERT_EQUAL_HEX32(LIT(1), out[5]);
    TEST_ASSERT_EQUAL_HEX32(LIT(1), out[6]);
    TEST_ASSERT_EQUAL_HEX32(LIT(0.5f), out[7]);     /* the LED it ends in, half lit */
    neopixel_render(&n->px, 0, true, out);
    TEST_ASSERT_EQUAL_STRING("", lit_leds(out, 20));
    neopixel_render(&n->px, 1, true, out);
    TEST_ASSERT_EQUAL_STRING("5 6 7 8 9", lit_leds(out, 20));
    neopixel_render(&n->px, 1, false, out);         /* switched off */
    TEST_ASSERT_EQUAL_STRING("", lit_leds(out, 20));

    /* Zigzag: row 2 runs right to left, so from the left is from LED 9. */
    n = compile_neopixel(&p, NEOPIXEL_ARRAY, NEOPIXEL_ZIGZAG, grid, 2, NEOPIXEL_BAR_ROW, 1, 0);
    TEST_ASSERT_EQUAL_UINT16(9, neopixel_array_led(&n->px, 1, 0));
    neopixel_render(&n->px, 0.4f, true, out);
    TEST_ASSERT_EQUAL_STRING("8 9", lit_leds(out, 20));
    /* Reversed: from the right. */
    n = compile_neopixel(&p, NEOPIXEL_ARRAY, NEOPIXEL_ZIGZAG, grid, 2, NEOPIXEL_BAR_ROW, 1, NEOPIXEL_GFX_REVERSE);
    neopixel_render(&n->px, 0.4f, true, out);
    TEST_ASSERT_EQUAL_STRING("5 6", lit_leds(out, 20));

    /* Up column 3 from the bottom; reversed, down from the top. */
    n = compile_neopixel(&p, NEOPIXEL_ARRAY, 0, grid, 2, NEOPIXEL_BAR_COLUMN, 2, 0);
    neopixel_render(&n->px, 0.5f, true, out);
    TEST_ASSERT_EQUAL_STRING("12 17", lit_leds(out, 20));
    n = compile_neopixel(&p, NEOPIXEL_ARRAY, 0, grid, 2, NEOPIXEL_BAR_COLUMN, 2, NEOPIXEL_GFX_REVERSE);
    neopixel_render(&n->px, 0.75f, true, out);
    TEST_ASSERT_EQUAL_STRING("2 7 12", lit_leds(out, 20));

    /* Brightness scales the colour: 51 of 255 is a fifth. */
    p.nodes[1].px.brightness = 51;
    neopixel_render(&p.nodes[1].px, 1, true, out);
    TEST_ASSERT_EQUAL_HEX32(LIT(0.2f), out[2]);

    /* A doughnut of 12 from LED 10: on round past LED 11 to 0; reversed, back to 9. */
    static const uint8_t ring[] = { 12 };
    n = compile_neopixel(&p, NEOPIXEL_DOUGHNUT, 0, ring, 1, NEOPIXEL_BAR_ROW, 10, 0);
    neopixel_render(&n->px, 0.25f, true, out);
    TEST_ASSERT_EQUAL_STRING("0 10 11", lit_leds(out, 12));
    n = compile_neopixel(&p, NEOPIXEL_DOUGHNUT, 0, ring, 1, NEOPIXEL_BAR_ROW, 10, NEOPIXEL_GFX_REVERSE);
    neopixel_render(&n->px, 0.25f, true, out);
    TEST_ASSERT_EQUAL_STRING("8 9 10", lit_leds(out, 12));

    /* A circle wired outside in, 8 then 4 then 1 (LEDs 0-7, 8-11, 12): around ring 2, or
       across the rings from the centre out (the ring of 1, then of 4, then of 8). */
    static const uint8_t rings[] = { 8, 4, 1 };
    n = compile_neopixel(&p, NEOPIXEL_CIRCLE, 0, rings, 3, NEOPIXEL_BAR_ROW, 1, 0);
    TEST_ASSERT_EQUAL_UINT16(13, n->px.leds);
    neopixel_render(&n->px, 0.5f, true, out);
    TEST_ASSERT_EQUAL_STRING("8 9", lit_leds(out, 13));
    n = compile_neopixel(&p, NEOPIXEL_CIRCLE, 0, rings, 3, NEOPIXEL_BAR_ROW, 1, NEOPIXEL_GFX_REVERSE);
    neopixel_render(&n->px, 0.5f, true, out);
    TEST_ASSERT_EQUAL_STRING("8 11", lit_leds(out, 13));
    n = compile_neopixel(&p, NEOPIXEL_CIRCLE, 0, rings, 3, NEOPIXEL_BAR_COLUMN, 0, 0);
    neopixel_render(&n->px, 0.5f, true, out);   /* 1.5 rings: the middle, and the ring of 4 half lit */
    TEST_ASSERT_EQUAL_STRING("8 9 10 11 12", lit_leds(out, 13));
    TEST_ASSERT_EQUAL_HEX32(LIT(1), out[12]);
    TEST_ASSERT_EQUAL_HEX32(LIT(0.5f), out[8]);
    n = compile_neopixel(&p, NEOPIXEL_CIRCLE, 0, rings, 3, NEOPIXEL_BAR_COLUMN, 0, NEOPIXEL_GFX_REVERSE);
    neopixel_render(&n->px, 0.33f, true, out);  /* outside in: the ring of 8, nearly all */
    TEST_ASSERT_EQUAL_STRING("0 1 2 3 4 5 6 7", lit_leds(out, 13));
}

/* The LEDs a point is on as its input goes 0, 0.25, 0.5, 0.75, 1. */
static const char *point_path(const prog_node *n)
{
    static char text[64];
    size_t len = 0;
    for (int i = 0; i <= 4; i++)
        len += (size_t)snprintf(text + len, sizeof text - len, "%s%u", i ? " " : "", neopixel_point_led(&n->px, i / 4.0f));
    return text;
}

void test_neopixel_point(void)
{
    static hw_program p;
    static uint32_t out[NEOPIXEL_LEDS_MAX];
    static const uint8_t grid[] = { 5, 5 };     /* 5 rows of 5: LED row × 5 + column */

    /* Across a row, down a column, and on a slant, either way. */
    TEST_ASSERT_EQUAL_STRING("10 11 12 13 14", point_path(compile_point(&p, NEOPIXEL_ARRAY, grid, 2, 2, 0, 2, 4, 0)));
    TEST_ASSERT_EQUAL_STRING("14 13 12 11 10", point_path(compile_point(&p, NEOPIXEL_ARRAY, grid, 2, 2, 4, 2, 0, 0)));
    TEST_ASSERT_EQUAL_STRING("21 16 11 6 1", point_path(compile_point(&p, NEOPIXEL_ARRAY, grid, 2, 4, 1, 0, 1, 0)));
    TEST_ASSERT_EQUAL_STRING("20 16 12 8 4", point_path(compile_point(&p, NEOPIXEL_ARRAY, grid, 2, 4, 0, 0, 4, 0)));

    /* One LED lit, in its colour. */
    const prog_node *n = compile_point(&p, NEOPIXEL_ARRAY, grid, 2, 0, 0, 4, 4, 0);
    neopixel_render(&n->px, 0.5f, true, out);
    TEST_ASSERT_EQUAL_STRING("12", lit_leds(out, 25));
    TEST_ASSERT_EQUAL_HEX32(LIT(1), out[12]);
    neopixel_render(&n->px, 0.5f, false, out);
    TEST_ASSERT_EQUAL_STRING("", lit_leds(out, 25));

    /* A doughnut of 8: from LED 2 to 6 the short way or (reversed) the other way round; from
       an LED to itself, once round. */
    static const uint8_t ring[] = { 8 };
    TEST_ASSERT_EQUAL_STRING("2 3 4 5 6", point_path(compile_point(&p, NEOPIXEL_DOUGHNUT, ring, 1, 0, 2, 0, 6, 0)));
    TEST_ASSERT_EQUAL_STRING("2 1 0 7 6", point_path(compile_point(&p, NEOPIXEL_DOUGHNUT, ring, 1, 0, 2, 0, 6, NEOPIXEL_GFX_REVERSE)));
    TEST_ASSERT_EQUAL_STRING("6 0 2 4 6", point_path(compile_point(&p, NEOPIXEL_DOUGHNUT, ring, 1, 0, 6, 0, 6, 0)));

    /* A circle wired outside in, 8, 4, 1 (LEDs 0-7, 8-11, 12): from the middle out to the
       outer ring's LED 2 (a quarter turn), and around the ring of 4. */
    static const uint8_t rings[] = { 8, 4, 1 };
    TEST_ASSERT_EQUAL_STRING("12 8 9 2 2", point_path(compile_point(&p, NEOPIXEL_CIRCLE, rings, 3, 2, 0, 0, 2, 0)));
    TEST_ASSERT_EQUAL_STRING("8 9 10 11 8", point_path(compile_point(&p, NEOPIXEL_CIRCLE, rings, 3, 1, 0, 1, 0, 0)));
}

/* Between two LEDs of its path, a point lights both, each as much as it is near it. */
void test_neopixel_point_blends(void)
{
    static hw_program p;
    static uint32_t out[NEOPIXEL_LEDS_MAX];
    static const uint8_t grid[] = { 5, 5 }, ring[] = { 8 }, rings[] = { 8, 4, 1 };

    /* Along row 3, 4 steps: at 0.375 half way from LED 11 to 12; at 0.3125 a quarter. */
    const prog_node *n = compile_point(&p, NEOPIXEL_ARRAY, grid, 2, 2, 0, 2, 4, 0);
    TEST_ASSERT_EQUAL_UINT16(4, neopixel_point_steps(&n->px));
    neopixel_render(&n->px, 0.375f, true, out);
    TEST_ASSERT_EQUAL_STRING("11 12", lit_leds(out, 25));
    TEST_ASSERT_EQUAL_HEX32(LIT(0.5f), out[11]);
    TEST_ASSERT_EQUAL_HEX32(LIT(0.5f), out[12]);
    neopixel_render(&n->px, 0.3125f, true, out);
    TEST_ASSERT_EQUAL_HEX32(LIT(0.75f), out[11]);
    TEST_ASSERT_EQUAL_HEX32(LIT(0.25f), out[12]);
    neopixel_render(&n->px, 1, true, out);         /* at its end: that LED alone */
    TEST_ASSERT_EQUAL_STRING("14", lit_leds(out, 25));
    TEST_ASSERT_EQUAL_HEX32(LIT(1), out[14]);

    /* On a slant, 4 steps, LED to LED along the diagonal: 20, 16, 12, 8, 4. */
    n = compile_point(&p, NEOPIXEL_ARRAY, grid, 2, 4, 0, 0, 4, 0);
    neopixel_render(&n->px, 0.125f, true, out);
    TEST_ASSERT_EQUAL_STRING("16 20", lit_leds(out, 25));
    TEST_ASSERT_EQUAL_HEX32(LIT(0.5f), out[16]);
    /* A slant of 2 rows over 4 columns: 4 steps, a row every other one. */
    n = compile_point(&p, NEOPIXEL_ARRAY, grid, 2, 0, 0, 2, 4, 0);
    TEST_ASSERT_EQUAL_UINT16(4, neopixel_point_steps(&n->px));
    TEST_ASSERT_EQUAL_STRING("0 6 7 13 14", point_path(n));   /* rows rounded, a half up */
    /* Start and end the same: no steps, one LED. */
    n = compile_point(&p, NEOPIXEL_ARRAY, grid, 2, 1, 1, 1, 1, 0);
    TEST_ASSERT_EQUAL_UINT16(0, neopixel_point_steps(&n->px));
    neopixel_render(&n->px, 0.3f, true, out);
    TEST_ASSERT_EQUAL_STRING("6", lit_leds(out, 25));

    /* A doughnut of 8 from LED 2 to 6: 4 steps; reversed the other way round, 4 too. */
    n = compile_point(&p, NEOPIXEL_DOUGHNUT, ring, 1, 0, 2, 0, 6, 0);
    TEST_ASSERT_EQUAL_UINT16(4, neopixel_point_steps(&n->px));
    neopixel_render(&n->px, 0.125f, true, out);
    TEST_ASSERT_EQUAL_STRING("2 3", lit_leds(out, 8));
    n = compile_point(&p, NEOPIXEL_DOUGHNUT, ring, 1, 0, 2, 0, 6, NEOPIXEL_GFX_REVERSE);
    neopixel_render(&n->px, 0.375f, true, out);
    TEST_ASSERT_EQUAL_STRING("0 1", lit_leds(out, 8));
    /* Once round from LED 6: 8 steps; near the end, between LED 5 and back at 6. */
    n = compile_point(&p, NEOPIXEL_DOUGHNUT, ring, 1, 0, 6, 0, 6, 0);
    TEST_ASSERT_EQUAL_UINT16(8, neopixel_point_steps(&n->px));
    neopixel_render(&n->px, 0.9375f, true, out);
    TEST_ASSERT_EQUAL_STRING("5 6", lit_leds(out, 8));
    TEST_ASSERT_EQUAL_HEX32(LIT(0.5f), out[5]);

    /* A circle from the middle out to the outer ring's LED 2: 2 rings, a quarter of 8 LEDs:
       2 steps, 12 then 9 then 2; a quarter of the way, between 12 and 9. */
    n = compile_point(&p, NEOPIXEL_CIRCLE, rings, 3, 2, 0, 0, 2, 0);
    TEST_ASSERT_EQUAL_UINT16(2, neopixel_point_steps(&n->px));
    neopixel_render(&n->px, 0.25f, true, out);
    TEST_ASSERT_EQUAL_STRING("9 12", lit_leds(out, 13));
    TEST_ASSERT_EQUAL_HEX32(LIT(0.5f), out[9]);
}

static const prog_node *compile_patch(hw_program *p, uint8_t layout, const uint8_t *size, uint8_t k,
                                      uint8_t a0, uint8_t b0, uint8_t a1, uint8_t b1, uint8_t gfx_options)
{
    const uint8_t set[] = { a0, b0, a1, b1, gfx_options, 10, 20, 30 };
    return compile_neopixel_gfx(p, layout, 0, size, k, NEOPIXEL_GFX_PATCH, set, sizeof set);
}

/* The LEDs a patch lights, at an input of 0.5. */
static const char *patch_leds(const prog_node *n, uint16_t leds)
{
    static uint32_t out[NEOPIXEL_LEDS_MAX];
    neopixel_render(&n->px, 0.5f, true, out);
    return lit_leds(out, leds);
}

/* A patch lights every LED from its start to its end, whatever its input. */
void test_neopixel_patch(void)
{
    static hw_program p;
    static uint32_t out[NEOPIXEL_LEDS_MAX];
    static const uint8_t grid[] = { 5, 5 }, ring[] = { 8 }, rings[] = { 8, 4, 1 };

    /* On an array, the rectangle with those corners, given either way round. */
    const prog_node *n = compile_patch(&p, NEOPIXEL_ARRAY, grid, 2, 1, 1, 2, 3, 0);
    TEST_ASSERT_EQUAL_STRING("6 7 8 11 12 13", patch_leds(n, 25));
    TEST_ASSERT_EQUAL_STRING("6 7 8 11 12 13", patch_leds(compile_patch(&p, NEOPIXEL_ARRAY, grid, 2, 2, 3, 1, 1, 0), 25));
    TEST_ASSERT_EQUAL_STRING("16 17 18 19", patch_leds(compile_patch(&p, NEOPIXEL_ARRAY, grid, 2, 3, 4, 3, 1, 0), 25));
    TEST_ASSERT_EQUAL_STRING("24", patch_leds(compile_patch(&p, NEOPIXEL_ARRAY, grid, 2, 4, 4, 4, 4, 0), 25));
    /* Its input doesn't move it, or dim it; off, it is dark. */
    n = compile_patch(&p, NEOPIXEL_ARRAY, grid, 2, 0, 0, 0, 1, 0);
    neopixel_render(&n->px, 0, true, out);
    TEST_ASSERT_EQUAL_STRING("0 1", lit_leds(out, 25));
    TEST_ASSERT_EQUAL_HEX32(LIT(1), out[1]);
    neopixel_render(&n->px, 1, true, out);
    TEST_ASSERT_EQUAL_HEX32(LIT(1), out[0]);
    neopixel_render(&n->px, 1, false, out);
    TEST_ASSERT_EQUAL_STRING("", lit_leds(out, 25));

    /* Its colour from its input: green 200 at 0 to red 200 at 1, on every LED of it. */
    const uint8_t fade[] = { 0, 0, 0, 1, NEOPIXEL_GFX_COLOUR_INPUT, 0, 200, 0, 200, 0, 0 };
    n = compile_neopixel_gfx(&p, NEOPIXEL_ARRAY, 0, grid, 2, NEOPIXEL_GFX_PATCH, fade, sizeof fade);
    neopixel_render(&n->px, 0.5f, true, out);
    TEST_ASSERT_EQUAL_HEX32(100u << 24 | 100u << 16, out[0]);
    TEST_ASSERT_EQUAL_HEX32(100u << 24 | 100u << 16, out[1]);
    neopixel_render(&n->px, 0, true, out);
    TEST_ASSERT_EQUAL_HEX32(200u << 24, out[1]);

    /* A doughnut of 8: from LED 6 round to 1 in wiring order, or reversed the other way;
       one LED; the whole ring. */
    TEST_ASSERT_EQUAL_STRING("0 1 6 7", patch_leds(compile_patch(&p, NEOPIXEL_DOUGHNUT, ring, 1, 0, 6, 0, 1, 0), 8));
    TEST_ASSERT_EQUAL_STRING("1 2 3 4 5 6",
                             patch_leds(compile_patch(&p, NEOPIXEL_DOUGHNUT, ring, 1, 0, 6, 0, 1, NEOPIXEL_GFX_REVERSE), 8));
    TEST_ASSERT_EQUAL_STRING("3", patch_leds(compile_patch(&p, NEOPIXEL_DOUGHNUT, ring, 1, 0, 3, 0, 3, 0), 8));
    TEST_ASSERT_EQUAL_STRING("0 1 2 3 4 5 6 7", patch_leds(compile_patch(&p, NEOPIXEL_DOUGHNUT, ring, 1, 0, 0, 0, 7, 0), 8));

    /* A circle wired outside in, 8, 4, 1 (LEDs 0-7, 8-11, 12): a quarter turn from LED 0
       on the ring of 4 out to the ring of 8: LEDs 8 and 9, and 0 to 2. From the middle,
       the middle too. */
    n = compile_patch(&p, NEOPIXEL_CIRCLE, rings, 3, 1, 0, 0, 2, 0);
    TEST_ASSERT_EQUAL_STRING("0 1 2 8 9", patch_leds(n, 13));
    uint8_t start, more;
    neopixel_patch_span(&n->px, 0, &start, &more);
    TEST_ASSERT_EQUAL_UINT8(0, start);
    TEST_ASSERT_EQUAL_UINT8(2, more);
    TEST_ASSERT_EQUAL_STRING("0 1 2 8 9 12", patch_leds(compile_patch(&p, NEOPIXEL_CIRCLE, rings, 3, 2, 0, 0, 2, 0), 13));
    /* The whole of the ring of 4 and the middle: from the middle to the ring of 4's LED 3. */
    TEST_ASSERT_EQUAL_STRING("8 9 10 11 12", patch_leds(compile_patch(&p, NEOPIXEL_CIRCLE, rings, 3, 2, 0, 1, 3, 0), 13));
}

void test_neopixel_colour_input_and_switch(void)
{
    static hw_program p;
    static uint32_t out[NEOPIXEL_LEDS_MAX];
    static const uint8_t strip[] = { 1, 4 };

    /* A bar from green at 0 to red at 1: half way, half of each, on every lit LED. */
    const uint8_t set[] = { NEOPIXEL_BAR_ROW, 0, NEOPIXEL_GFX_COLOUR_INPUT, 0, 200, 0, 200, 0, 0 };
    const prog_node *n = compile_neopixel_gfx(&p, NEOPIXEL_ARRAY, 0, strip, 2, NEOPIXEL_GFX_BAR, set, sizeof set);
    neopixel_render(&n->px, 0.5f, true, out);
    TEST_ASSERT_EQUAL_STRING("0 1", lit_leds(out, 4));
    TEST_ASSERT_EQUAL_HEX32(100u << 24 | 100u << 16, out[0]);
    neopixel_render(&n->px, 1, true, out);
    TEST_ASSERT_EQUAL_HEX32(200u << 16, out[3]);
    neopixel_render(&n->px, 0.25f, true, out);
    TEST_ASSERT_EQUAL_HEX32(150u << 24 | 50u << 16, out[0]);

    /* The on/off input: wired to button n3, on from 0.5; not wired, always on. */
    TEST_ASSERT_EQUAL_UINT8(2, n->in_count);
    TEST_ASSERT_EQUAL_UINT8(program_signal(&p, 3, 0), n->in[1]);
    TEST_ASSERT_FALSE(neopixel_enabled(n, 0));
    TEST_ASSERT_TRUE(neopixel_enabled(n, 1));
    p.nodes[1].in[1] = PROG_NO_SIGNAL;
    TEST_ASSERT_TRUE(neopixel_enabled(n, 0));
}

/* A NeoPixel message: node `id` on a 4 × 4 array (or `size`), from ADC n1, a graphic,
   and with `under` its data line in. Returns its length. */
static uint8_t chained_neopixel(uint8_t *msg, uint16_t id, uint16_t under, const uint8_t *size,
                                uint8_t graphic, const uint8_t *set, uint8_t glen)
{
    msg[0] = MSG_HW_NEOPIXEL;
    msg[1] = MSG_FLAG_OUTPUT;
    msg[2] = (uint8_t)(id >> 8);
    msg[3] = (uint8_t)id;
    uint8_t len = neopixel_pay(msg + 5, NEOPIXEL_ARRAY, 0, size, 2, graphic, set, glen);
    if (under) {
        msg[5 + len] = (uint8_t)(under >> 8);
        msg[6 + len] = (uint8_t)under;
        msg[7 + len] = 0;
        len += 3;
    }
    msg[4] = len;
    return (uint8_t)(5 + len);
}

void test_neopixel_chain(void)
{
    static hw_program p;
    static const uint8_t grid[] = { 4, 4 }, small[] = { 2, 2 };
    static const uint8_t patch[] = { 0, 0, 3, 3, 0, 10, 20, 30 };          /* all of it, red 10, green 20, blue 30 */
    static const uint8_t point[] = { 0, 0, 0, 3, 0, 200, 0, 0 };           /* along the top row, red 200 */
    uint8_t cfg[512] = { MSG_HW_ADC, 0, 0, 1, 1, 0 };
    size_t n = 6;
    n += chained_neopixel(cfg + n, 2, 0, grid, NEOPIXEL_GFX_PATCH, patch, sizeof patch);  /* the first */
    n += chained_neopixel(cfg + n, 3, 2, grid, NEOPIXEL_GFX_POINT, point, sizeof point);  /* over n2 */
    n += chained_neopixel(cfg + n, 4, 3, small, NEOPIXEL_GFX_PATCH, (const uint8_t[]){ 0, 0, 1, 1, 0, 1, 2, 3 }, 8);
    n += chained_neopixel(cfg + n, 5, 2, grid, NEOPIXEL_GFX_POINT, point, sizeof point);  /* n2 feeds n3 already */
    n += chained_neopixel(cfg + n, 6, 6, grid, NEOPIXEL_GFX_POINT, point, sizeof point);  /* itself */
    n += chained_neopixel(cfg + n, 7, 8, grid, NEOPIXEL_GFX_POINT, point, sizeof point);  /* n7 and n8: a loop */
    n += chained_neopixel(cfg + n, 8, 7, grid, NEOPIXEL_GFX_POINT, point, sizeof point);
    n += chained_neopixel(cfg + n, 9, 30, grid, NEOPIXEL_GFX_POINT, point, sizeof point); /* no such node */
    program_compile(cfg, n, &p);
    TEST_ASSERT_EQUAL_UINT8(9, p.node_count);
    const prog_node *first = &p.nodes[1], *over = &p.nodes[2];
    TEST_ASSERT_TRUE(first->px_feeds);
    TEST_ASSERT_EQUAL_UINT8(PROG_NO_NODE, first->px_under);
    TEST_ASSERT_EQUAL_UINT8(1, over->px_under);
    TEST_ASSERT_FALSE(over->px_feeds);                      /* n4's LEDs differ: it isn't joined */
    TEST_ASSERT_EQUAL_UINT8(PROG_NO_NODE, p.nodes[3].px_under);
    TEST_ASSERT_EQUAL_UINT8(PROG_NO_NODE, p.nodes[4].px_under);
    TEST_ASSERT_EQUAL_UINT8(PROG_NO_NODE, p.nodes[5].px_under);
    TEST_ASSERT_EQUAL_UINT8(7, p.nodes[6].px_under);        /* n7 over n8; n8 over n7 would close the loop */
    TEST_ASSERT_TRUE(p.nodes[7].px_feeds);
    TEST_ASSERT_EQUAL_UINT8(PROG_NO_NODE, p.nodes[7].px_under);
    TEST_ASSERT_EQUAL_UINT8(PROG_NO_NODE, p.nodes[8].px_under);
    static char text[2048];
    program_describe(&p, text, sizeof text);
    TEST_ASSERT_NOT_NULL(strstr(text, "NEOPIXEL n2 pin 16 array 4x4 brightness 255 patch 0,0-3,3 colour 0A141E <- s0 / 3.3 on <- - feeds\n"));
    TEST_ASSERT_NOT_NULL(strstr(text, "NEOPIXEL n3 pin 16 array 4x4 brightness 255 point 0,0-0,3 colour C80000 <- s0 / 3.3 on <- - over n2\n"));

    /* The point over the patch: half way, between LEDs 1 and 2, each half way from the
       patch's colour to its own; the rest of the patch shows through. */
    uint32_t out[16];
    memset(out, 0, sizeof out);
    neopixel_draw(&first->px, 0, true, out);
    neopixel_draw(&over->px, 0.5f, true, out);
    TEST_ASSERT_EQUAL_HEX32(LIT(1), out[0]);
    TEST_ASSERT_EQUAL_HEX32(10u << 24 | 105u << 16 | 15u << 8, out[1]);
    TEST_ASSERT_EQUAL_HEX32(10u << 24 | 105u << 16 | 15u << 8, out[2]);
    TEST_ASSERT_EQUAL_HEX32(LIT(1), out[3]);
    TEST_ASSERT_EQUAL_HEX32(LIT(1), out[15]);
    /* On the end: all its colour. Off: it draws nothing, and the patch shows. */
    neopixel_draw(&over->px, 1, true, out);
    TEST_ASSERT_EQUAL_HEX32(200u << 16, out[3]);
    memset(out, 0, sizeof out);
    neopixel_draw(&first->px, 0, true, out);
    neopixel_draw(&over->px, 1, false, out);
    TEST_ASSERT_EQUAL_HEX32(LIT(1), out[3]);
    /* neopixel_render is a draw over all off. */
    neopixel_render(&over->px, 1, true, out);
    TEST_ASSERT_EQUAL_HEX32(200u << 16, out[3]);
    TEST_ASSERT_EQUAL_HEX32(0, out[0]);
}

/* ── DS18B20 temperature sensors ──────────────────────────────────────── */

/* A scratchpad holding a temperature (its LSB and MSB), with its CRC. */
static void ds18b20_pad(uint8_t pad[DS18B20_PAD_LEN], uint8_t lsb, uint8_t msb)
{
    const uint8_t head[] = { lsb, msb, 0x4B, 0x46, 0x7F, 0xFF, 0x0C, 0x10 };
    memcpy(pad, head, sizeof head);
    pad[8] = ds18b20_crc8(pad, 8);
}

void test_ds18b20_scratchpad(void)
{
    /* The CRC of Maxim's application note 27 example: a ROM of 02 1C B8 01 00 00 00 has CRC A2. */
    static const uint8_t rom[] = { 0x02, 0x1C, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xA2 };
    TEST_ASSERT_EQUAL_HEX8(0xA2, ds18b20_crc8(rom, 7));
    TEST_ASSERT_EQUAL_HEX8(0, ds18b20_crc8(rom, 8));

    uint8_t pad[DS18B20_PAD_LEN];
    float c = 0;
    ds18b20_pad(pad, 0x91, 0x01);                       /* +25.0625 °C (the datasheet's table) */
    TEST_ASSERT_TRUE(ds18b20_celsius(pad, DS18B20_RES_12, &c));
    TEST_ASSERT_EQUAL_FLOAT(25.0625f, c);
    TEST_ASSERT_TRUE(ds18b20_celsius(pad, DS18B20_RES_11, &c));
    TEST_ASSERT_EQUAL_FLOAT(25.0f, c);                  /* the low bit is undefined at 11 bits */
    TEST_ASSERT_TRUE(ds18b20_celsius(pad, DS18B20_RES_9, &c));
    TEST_ASSERT_EQUAL_FLOAT(25.0f, c);
    ds18b20_pad(pad, 0x5E, 0xFF);                       /* −10.125 °C */
    TEST_ASSERT_TRUE(ds18b20_celsius(pad, DS18B20_RES_12, &c));
    TEST_ASSERT_EQUAL_FLOAT(-10.125f, c);
    TEST_ASSERT_TRUE(ds18b20_celsius(pad, DS18B20_RES_9, &c));
    TEST_ASSERT_EQUAL_FLOAT(-10.5f, c);
    ds18b20_pad(pad, 0x90, 0xFC);                       /* −55 °C, its lowest */
    TEST_ASSERT_TRUE(ds18b20_celsius(pad, DS18B20_RES_12, &c));
    TEST_ASSERT_EQUAL_FLOAT(-55.0f, c);
    ds18b20_pad(pad, 0xD0, 0x07);                       /* +125 °C, its highest */
    TEST_ASSERT_TRUE(ds18b20_celsius(pad, DS18B20_RES_12, &c));
    TEST_ASSERT_EQUAL_FLOAT(125.0f, c);

    /* A bit wrong anywhere fails the CRC; a line held low, or left high, is no sensor. */
    c = 1;
    pad[4] ^= 0x01;
    TEST_ASSERT_FALSE(ds18b20_celsius(pad, DS18B20_RES_12, &c));
    TEST_ASSERT_EQUAL_FLOAT(1, c);
    memset(pad, 0, sizeof pad);
    TEST_ASSERT_FALSE(ds18b20_celsius(pad, DS18B20_RES_12, &c));
    memset(pad, 0xFF, sizeof pad);
    TEST_ASSERT_FALSE(ds18b20_celsius(pad, DS18B20_RES_12, &c));

    TEST_ASSERT_EQUAL_UINT16(94, ds18b20_conversion_ms(DS18B20_RES_9));
    TEST_ASSERT_EQUAL_UINT16(750, ds18b20_conversion_ms(DS18B20_RES_12));
    TEST_ASSERT_EQUAL_HEX8(0x1F, ds18b20_config(DS18B20_RES_9));
    TEST_ASSERT_EQUAL_HEX8(0x7F, ds18b20_config(DS18B20_RES_12));

    /* 85 °C, its power-on value: dropped once, unless the last reading was near it. */
    bool doubted = false;
    TEST_ASSERT_TRUE(ds18b20_take(25, true, 24, &doubted));
    TEST_ASSERT_FALSE(ds18b20_take(85, true, 25, &doubted));
    TEST_ASSERT_TRUE(doubted);
    TEST_ASSERT_TRUE(ds18b20_take(85, true, 25, &doubted));     /* again in a row: it is 85 °C */
    TEST_ASSERT_FALSE(doubted);
    TEST_ASSERT_FALSE(ds18b20_take(85, false, 0, &doubted));    /* a first reading */
    TEST_ASSERT_TRUE(ds18b20_take(24, false, 0, &doubted));
    TEST_ASSERT_TRUE(ds18b20_take(85, true, 84.5f, &doubted));  /* rising through it */
}

void test_ds18b20_program(void)
{
    static hw_program p;
    const uint8_t cfg[] = {
        MSG_HW_DS18B20, 0, 0, 1, 2, 15, DS18B20_RES_10 | DS18B20_PULLUP,     /* DS18B20 n1, GPIO 15 */
        MSG_HW_PWM, MSG_FLAG_OUTPUT, 0, 2, 17, 0, 1, 0, 2, 0x03, 0xE8, PWM_DUTY_INPUT, 50, 0, 100, 0, 0, 0, 0, 100, 0x27, 0x10,
        MSG_HW_GPO, MSG_FLAG_OUTPUT, 0, 3, 4, 0, 1, 1, 5,                   /* GPO n3 on GPIO 5 <- n1's found */
        MSG_HW_DS18B20, 0, 0, 4, 2, 25, 0,                                   /* GPIO 25: not usable */
        MSG_HW_DS18B20, 0, 0, 5, 2, 16, 0x08,                                /* an unknown option */
        MSG_HW_DS18B20, 0, 0, 6, 1, 16,                                      /* no options */
    };
    program_compile(cfg, sizeof cfg, &p);
    TEST_ASSERT_EQUAL_UINT8(2, p.signal_count);
    TEST_ASSERT_EQUAL_UINT8(3, p.node_count);
    const prog_node *ds = &p.nodes[0];
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_DS18B20, ds->type);
    TEST_ASSERT_EQUAL_UINT8(15, ds->pin);
    TEST_ASSERT_EQUAL_UINT8(program_signal(&p, 1, 0), ds->out);
    TEST_ASSERT_EQUAL_UINT8(program_signal(&p, 1, 1), ds->out_count);
    /* A PWM takes its temperature as a fraction of DS18B20_FULL; a GPO reads its found. */
    TEST_ASSERT_EQUAL_FLOAT(DS18B20_FULL, p.nodes[1].in_full[0]);
    TEST_ASSERT_EQUAL_UINT8(ds->out_count, p.nodes[2].in[0]);
    char text[512];
    program_describe(&p, text, sizeof text);
    TEST_ASSERT_NOT_NULL(strstr(text, "DS18B20 n1 pin 15 10-bit pull-up -> s0 found s1\n"));
}

/* ── Sine ─────────────────────────────────────────────────────────────── */

void test_sine(void)
{
    static hw_program p;
    /* Sine n2 <- button n1: a 40 ms period (4 scans), amplitude 2, offset 1. */
    const uint8_t cfg[] = {
        MSG_UI_BUTTON, 0, 0, 1, 0,
        MSG_LOGIC_SINE, MSG_FLAG_OUTPUT, 0, 2, 15, 0, 1, 0, 0, 0, 0, 40, 0x40, 0, 0, 0, 0x3F, 0x80, 0, 0,
        MSG_LOGIC_SINE, MSG_FLAG_OUTPUT, 0, 3, 15, 0, 0, 0, 0, 0, 0, 19, 0x40, 0, 0, 0, 0x3F, 0x80, 0, 0,   /* too short */
        MSG_LOGIC_SINE, MSG_FLAG_OUTPUT, 0, 4, 15, 0, 0, 0, 0, 0, 0, 40, 0x7F, 0xC0, 0, 0, 0, 0, 0, 0,     /* NaN amplitude */
        MSG_LOGIC_SINE, MSG_FLAG_OUTPUT, 0, 5, 11, 0, 0, 0, 0, 0, 0, 40, 0x40, 0, 0, 0,                    /* no offset */
    };
    program_compile(cfg, sizeof cfg, &p);
    TEST_ASSERT_EQUAL_UINT8(2, p.signal_count);
    TEST_ASSERT_EQUAL_UINT8(1, p.node_count);
    TEST_ASSERT_TRUE(program_logic_floating(MSG_LOGIC_SINE));
    const prog_node *n = &p.nodes[0];
    TEST_ASSERT_EQUAL_HEX8(MSG_LOGIC_SINE, n->type);
    TEST_ASSERT_EQUAL_UINT32(40, n->on_ms);

    /* Off: its offset. On: from its offset, up to offset + amplitude a quarter period
       on, back, down to offset - amplitude, and round again. */
    prog_logic_state st;
    program_logic_start(&p, n, &st);
    float out = 0;
    out = program_logic_step(&p, n, out, &st, 10);
    TEST_ASSERT_EQUAL_FLOAT(1, out);
    p.signals[0].value = 1;
    const float wave[] = { 1, 3, 1, -1, 1, 3 };
    for (int i = 0; i < 6; i++) {
        out = program_logic_step(&p, n, out, &st, 10);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, wave[i], out);
    }
    /* Off again: its offset; on, it starts from the beginning. */
    p.signals[0].value = 0;
    TEST_ASSERT_EQUAL_FLOAT(1, program_logic_step(&p, n, out, &st, 10));
    p.signals[0].value = 1;
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1, program_logic_step(&p, n, out, &st, 10));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3, program_logic_step(&p, n, out, &st, 10));

    char text[256];
    program_describe(&p, text, sizeof text);
    TEST_ASSERT_NOT_NULL(strstr(text, "SINE n2 period 40 amplitude 2 offset 1 phase 0 <- s0 -> s1\n"));

    /* A phase of 90°: it starts, and rests, at its top; one saved with its phase. */
    const uint8_t phased[] = {
        MSG_LOGIC_SINE, MSG_FLAG_OUTPUT, 0, 1, 19, 0, 0, 0, 0, 0, 0, 40, 0x40, 0, 0, 0, 0x3F, 0x80, 0, 0, 0x42, 0xB4, 0, 0,
    };
    TEST_ASSERT_TRUE(logic_payload_valid(MSG_LOGIC_SINE, phased + 5, 19, MSG_FLAG_OUTPUT));
    program_compile(phased, sizeof phased, &p);
    TEST_ASSERT_EQUAL_FLOAT(90, p.nodes[0].scale[2]);
    program_logic_start(&p, &p.nodes[0], &st);
    const float shifted[] = { 3, 1, -1, 1, 3 };
    for (int i = 0; i < 5; i++)
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, shifted[i], program_logic_step(&p, &p.nodes[0], 0, &st, 10));
    program_describe(&p, text, sizeof text);
    TEST_ASSERT_NOT_NULL(strstr(text, "SINE n1 period 40 amplitude 2 offset 1 phase 90 <- - -> s0\n"));
    const uint8_t phase_nan[] = { 0, 0, 0, 0, 0, 0, 40, 0x40, 0, 0, 0, 0x3F, 0x80, 0, 0, 0x7F, 0xC0, 0, 0 };
    TEST_ASSERT_FALSE(logic_payload_valid(MSG_LOGIC_SINE, phase_nan, 19, MSG_FLAG_OUTPUT));

    /* With nothing wired in it always runs. */
    const uint8_t free_running[] = {
        MSG_LOGIC_SINE, MSG_FLAG_OUTPUT, 0, 1, 15, 0, 0, 0, 0, 0, 0, 40, 0x40, 0, 0, 0, 0x3F, 0x80, 0, 0,
    };
    program_compile(free_running, sizeof free_running, &p);
    program_logic_start(&p, &p.nodes[0], &st);
    program_logic_step(&p, &p.nodes[0], 0, &st, 10);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3, program_logic_step(&p, &p.nodes[0], 0, &st, 10));
}

/* ── The clock ────────────────────────────────────────────────────────── */

void test_clock(void)
{
    uint8_t parts[3];
    clock_parts(0, parts);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(((uint8_t[]){ 0, 0, 0 }), parts, 3);
    clock_parts((13 * 3600 + 5 * 60 + 9) * 1000u + 999, parts);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(((uint8_t[]){ 13, 5, 9 }), parts, 3);
    clock_parts(CLOCK_DAY_MS - 1, parts);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(((uint8_t[]){ 23, 59, 59 }), parts, 3);

    /* Set at a timer count of 10 days and 5 s: then that time, and on from there, round
       midnight. */
    uint64_t now = 10ull * CLOCK_DAY_MS + 5000;
    uint32_t offset = 0;
    TEST_ASSERT_TRUE(clock_offset_for(now, 23, 59, 58, &offset));
    TEST_ASSERT_EQUAL_UINT32((23 * 3600 + 59 * 60 + 58) * 1000u, clock_time_of_day(now, offset));
    clock_parts(clock_time_of_day(now + 3000, offset), parts);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(((uint8_t[]){ 0, 0, 1 }), parts, 3);
    TEST_ASSERT_FALSE(clock_offset_for(now, 24, 0, 0, &offset));
    TEST_ASSERT_FALSE(clock_offset_for(now, 0, 60, 0, &offset));
    TEST_ASSERT_FALSE(clock_offset_for(now, 0, 0, 60, &offset));
    TEST_ASSERT_EQUAL_UINT32((23 * 3600 + 59 * 60 + 58) * 1000u, clock_time_of_day(now, offset));   /* left */

    /* A clock node: three signals in a row; a PWM takes its hour as a fraction of 24. */
    static hw_program p;
    const uint8_t cfg[] = {
        MSG_HW_CLOCK, MSG_FLAG_SHOWN, 0, 1, 0,
        MSG_HW_PWM, MSG_FLAG_OUTPUT, 0, 2, 17, 0, 1, 0, 2, 0x03, 0xE8, PWM_DUTY_INPUT | PWM_FREQ_INPUT, 50, 0, 100,
        0, 1, 2, 0, 100, 0x27, 0x10,                                         /* duty <- its hour, freq <- its second */
        MSG_HW_CLOCK, 0, 0, 3, 1, 0,                                         /* a payload: skipped */
    };
    program_compile(cfg, sizeof cfg, &p);
    TEST_ASSERT_EQUAL_UINT8(3, p.signal_count);
    TEST_ASSERT_EQUAL_UINT8(2, p.node_count);
    TEST_ASSERT_EQUAL_UINT8(0, p.nodes[0].out);
    TEST_ASSERT_EQUAL_UINT8(2, program_signal(&p, 1, 2));
    TEST_ASSERT_EQUAL_FLOAT(CLOCK_HOURS, p.nodes[1].in_full[0]);
    TEST_ASSERT_EQUAL_FLOAT(CLOCK_SECONDS, p.nodes[1].in_full[1]);
    char text[512];
    program_describe(&p, text, sizeof text);
    TEST_ASSERT_NOT_NULL(strstr(text, "CLOCK n1 -> s0 s1 s2\n"));
}

/* ── Rotary encoders ──────────────────────────────────────────────────── */

/* Feed a decoder its pins' states in turn (A in bit 1, B in bit 0); the clicks they make. */
static int32_t encoder_feed(encoder_decoder *d, const uint8_t *ab, int n, uint8_t detent)
{
    int32_t clicks = 0;
    for (int i = 0; i < n; i++) clicks += encoder_decode(d, ab[i], detent);
    return clicks;
}

void test_encoder_decode(void)
{
    /* From rest (both high), A falls first: clockwise on a KY-040, +1. */
    static const uint8_t cw[] = { 1, 0, 2, 3 }, ccw[] = { 2, 0, 1, 3 };
    encoder_decoder d;
    encoder_start(&d, 3);
    TEST_ASSERT_EQUAL_INT32(1, encoder_feed(&d, cw, 4, ENCODER_DETENT_4));
    TEST_ASSERT_EQUAL_INT32(1, encoder_feed(&d, cw, 4, ENCODER_DETENT_4));
    TEST_ASSERT_EQUAL_INT32(-1, encoder_feed(&d, ccw, 4, ENCODER_DETENT_4));
    /* Only once it rests: three transitions are no click yet. */
    TEST_ASSERT_EQUAL_INT32(0, encoder_feed(&d, cw, 3, ENCODER_DETENT_4));
    TEST_ASSERT_EQUAL_INT32(1, encoder_feed(&d, cw + 3, 1, ENCODER_DETENT_4));

    /* A bouncing contact goes to and fro: it nets out, and back at rest is no click. */
    static const uint8_t bounce[] = { 1, 3, 1, 3, 1, 3 };
    TEST_ASSERT_EQUAL_INT32(0, encoder_feed(&d, bounce, 6, ENCODER_DETENT_4));
    /* Half way and back: none. */
    static const uint8_t back[] = { 1, 0, 1, 3 };
    TEST_ASSERT_EQUAL_INT32(0, encoder_feed(&d, back, 4, ENCODER_DETENT_4));
    /* A missed edge (both pins at once counts 0) still clicks: 3 → 1 → 2 → 3 is +1 0 +1. */
    static const uint8_t missed[] = { 1, 2, 3 };
    TEST_ASSERT_EQUAL_INT32(1, encoder_feed(&d, missed, 3, ENCODER_DETENT_4));

    /* Half a cycle a click: it rests at 00 too. */
    encoder_start(&d, 3);
    TEST_ASSERT_EQUAL_INT32(2, encoder_feed(&d, cw, 4, ENCODER_DETENT_2));
    TEST_ASSERT_EQUAL_INT32(-2, encoder_feed(&d, ccw, 4, ENCODER_DETENT_2));
    /* Each edge: every transition. */
    TEST_ASSERT_EQUAL_INT32(4, encoder_feed(&d, cw, 4, ENCODER_DETENT_1));
    TEST_ASSERT_EQUAL_INT32(-4, encoder_feed(&d, ccw, 4, ENCODER_DETENT_1));
    TEST_ASSERT_EQUAL_INT8(0, encoder_decode(&d, 3, ENCODER_DETENT_1));    /* no change */
    TEST_ASSERT_EQUAL_INT8(0, encoder_decode(&d, 0, ENCODER_DETENT_1));    /* both pins */

    /* Its position: clamped to 0 to its steps, or wrapped round 0 to steps - 1. */
    TEST_ASSERT_EQUAL_INT32(5, encoder_position(3, 2, 20, false));
    TEST_ASSERT_EQUAL_INT32(0, encoder_position(1, -3, 20, false));
    TEST_ASSERT_EQUAL_INT32(20, encoder_position(19, 4, 20, false));
    TEST_ASSERT_EQUAL_INT32(15, encoder_position(30, 0, 15, false));    /* fewer steps after a save */
    TEST_ASSERT_EQUAL_INT32(1, encoder_position(19, 2, 20, true));
    TEST_ASSERT_EQUAL_INT32(18, encoder_position(1, -3, 20, true));
    TEST_ASSERT_EQUAL_INT32(0, encoder_position(19, 1, 20, true));
    TEST_ASSERT_EQUAL_INT32(19, encoder_position(0, -41, 20, true));
    TEST_ASSERT_EQUAL_INT32(0, encoder_position(7, 3, 1, true));
}

void test_encoder_program(void)
{
    static hw_program p;
    const uint8_t cfg[] = {
        MSG_HW_ENCODER, 0, 0, 1, 6, 6, 7, 8, ENCODER_WRAP | ENCODER_DETENT_2, 0, 24,     /* n1: 24 steps */
        MSG_HW_SERVO, MSG_FLAG_OUTPUT, 0, 2, 12, 0, 1, 0, 9, 0, 180, 0x01, 0xF4, 0x09, 0xC4, 0, 0,
        MSG_HW_GPO, MSG_FLAG_OUTPUT, 0, 3, 4, 0, 1, 1, 5,                   /* GPO n3 on GPIO 5 <- n1's pressed */
        MSG_HW_ENCODER, 0, 0, 4, 6, 10, 11, ENCODER_NO_BUTTON, ENCODER_REVERSE, 0x03, 0xE8,  /* no switch, 1000 */
        MSG_HW_ENCODER, 0, 0, 5, 6, 12, 12, 13, 0, 0, 20,                    /* A and B on one pin */
        MSG_HW_ENCODER, 0, 0, 6, 6, 12, 13, 12, 0, 0, 20,                    /* the switch on A */
        MSG_HW_ENCODER, 0, 0, 7, 6, 12, 13, 25, 0, 0, 20,                    /* GPIO 25: not usable */
        MSG_HW_ENCODER, 0, 0, 8, 6, 12, 13, 14, 0x03, 0, 20,                 /* no such detent */
        MSG_HW_ENCODER, 0, 0, 9, 6, 12, 13, 14, 0x10, 0, 20,                 /* an unknown option */
        MSG_HW_ENCODER, 0, 0, 10, 6, 12, 13, 14, 0, 0, 0,                    /* no steps */
        MSG_HW_ENCODER, 0, 0, 11, 6, 12, 13, 14, 0, 0x03, 0xE9,              /* over ENCODER_STEPS_MAX */
        MSG_HW_ENCODER, 0, 0, 12, 5, 12, 13, 14, 0, 0,                       /* too short */
    };
    program_compile(cfg, sizeof cfg, &p);
    TEST_ASSERT_EQUAL_UINT8(4, p.signal_count);
    TEST_ASSERT_EQUAL_UINT8(4, p.node_count);
    const prog_node *enc = &p.nodes[0];
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_ENCODER, enc->type);
    TEST_ASSERT_EQUAL_UINT8(6, enc->pin);
    TEST_ASSERT_EQUAL_UINT8(8, enc->enc_pins[2]);
    TEST_ASSERT_EQUAL_UINT16(24, enc->enc_steps);
    TEST_ASSERT_EQUAL_UINT8(program_signal(&p, 1, 0), enc->out);
    TEST_ASSERT_EQUAL_UINT8(program_signal(&p, 1, 1), enc->out_count);
    TEST_ASSERT_EQUAL_UINT16(1000, p.nodes[1].enc_steps);
    /* A servo takes its position as a fraction of its steps; a GPO reads its pressed. */
    TEST_ASSERT_EQUAL_FLOAT(24, p.nodes[2].in_full[0]);
    TEST_ASSERT_EQUAL_UINT8(enc->out_count, p.nodes[3].in[0]);
    char text[512];
    program_describe(&p, text, sizeof text);
    TEST_ASSERT_NOT_NULL(strstr(text, "ENCODER n1 pins 6 7 button 8 2/click steps 24 wrap -> s0 pressed s1\n"));
    TEST_ASSERT_NOT_NULL(strstr(text, "ENCODER n4 pins 10 11 no button 4/click steps 1000 reverse -> s2 pressed s3\n"));
}

/* A counter: its count and reset references, then a preset from 1 to COUNTER_MAX. */
void test_counter_payload_valid(void)
{
    const uint8_t F = MSG_FLAG_OUTPUT;
    uint8_t counter[] = { 0, 1, 0,  0, 2, 0,  0, 0, 0, 3 };
    TEST_ASSERT_TRUE(logic_payload_valid(MSG_LOGIC_COUNTER, counter, sizeof counter, F));
    TEST_ASSERT_FALSE(logic_payload_valid(MSG_LOGIC_COUNTER, counter, sizeof counter - 1, F));
    counter[9] = 0;                                                     /* a preset of 0 */
    TEST_ASSERT_FALSE(logic_payload_valid(MSG_LOGIC_COUNTER, counter, sizeof counter, F));
    counter[6] = 0x00; counter[7] = 0x0F; counter[8] = 0x42; counter[9] = 0x41;     /* COUNTER_MAX + 1 */
    TEST_ASSERT_FALSE(logic_payload_valid(MSG_LOGIC_COUNTER, counter, sizeof counter, F));
    counter[9] = 0x40;                                                  /* COUNTER_MAX */
    TEST_ASSERT_TRUE(logic_payload_valid(MSG_LOGIC_COUNTER, counter, sizeof counter, F));
}

/* A feedback wire's reference has MSG_REF_FEEDBACK in its output byte. */
void test_feedback_reference(void)
{
    const uint8_t ref[] = { 0x01, 0x02, MSG_REF_FEEDBACK | 1 };
    msg_ref r = msg_ref_at(ref, MSG_FLAG_OUTPUT);
    TEST_ASSERT_EQUAL_HEX16(0x0102, r.id);
    TEST_ASSERT_EQUAL_UINT8(1, r.output);
    TEST_ASSERT_TRUE(r.feedback);
    r = msg_ref_at(ref, 0);                                             /* 2 bytes: output 0, no feedback */
    TEST_ASSERT_EQUAL_UINT8(0, r.output);
    TEST_ASSERT_FALSE(r.feedback);
}

/* One scan of a program's logic, as core 1 runs it (scan in src/hwman.c). */
static void scan_logic(hw_program *p, prog_logic_state *st)
{
    for (uint8_t i = 0; i < p->node_count; i++) {
        const prog_node *n = &p->nodes[i];
        if (!program_logic(n->type)) continue;
        p->signals[n->out].value = program_logic_step(p, n, p->signals[n->out].value, &st[i], 10);
        if (n->out_count != PROG_NO_SIGNAL) p->signals[n->out_count].value = (float)st[i].count;
    }
}

/* A counter counts its input going on up to its preset, and is on there; reset wins. */
void test_program_counter(void)
{
    static const uint8_t cfg[] = {
        MSG_UI_BUTTON, 0, 0, 1, 0,                                          /* button n1 */
        MSG_UI_BUTTON, 0, 0, 2, 0,                                          /* button n2 */
        MSG_LOGIC_COUNTER, MSG_FLAG_OUTPUT, 0, 3, 10, 0, 1, 0,  0, 2, 0,    /* counter n3: count <- n1, reset <- n2, */
            0, 0, 0, 3,                                                     /*   preset 3 */
    };
    static hw_program p;
    static prog_logic_state st[PROG_NODES_MAX];
    program_compile(cfg, sizeof cfg, &p);
    TEST_ASSERT_EQUAL_UINT8(4, p.signal_count);                         /* the buttons, done and the count */
    TEST_ASSERT_EQUAL_UINT8(1, p.node_count);
    const prog_node *c = &p.nodes[0];
    TEST_ASSERT_EQUAL_UINT8(program_signal(&p, 3, 0), c->out);
    TEST_ASSERT_EQUAL_UINT8(program_signal(&p, 3, 1), c->out_count);
    TEST_ASSERT_EQUAL_UINT32(3, c->preset);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, (float)c->preset);

    p.signals[0].value = 1;                                             /* on at the start: not counted */
    program_logic_start(&p, c, &st[0]);
    scan_logic(&p, st);
    TEST_ASSERT_EQUAL_FLOAT(0, p.signals[c->out_count].value);
    const float presses[] = { 0, 1, 1, 0, 1, 0, 1, 0, 1 };              /* goes on 4 times */
    const float counts[] = { 0, 1, 1, 1, 2, 2, 3, 3, 3 };
    for (int i = 0; i < 9; i++) {
        p.signals[0].value = presses[i];
        scan_logic(&p, st);
        TEST_ASSERT_EQUAL_FLOAT(counts[i], p.signals[c->out_count].value);
        TEST_ASSERT_EQUAL_FLOAT(counts[i] >= 3, p.signals[c->out].value);
    }
    p.signals[1].value = 1;                                             /* reset, while counting */
    p.signals[0].value = 0; scan_logic(&p, st);
    p.signals[0].value = 1; scan_logic(&p, st);
    TEST_ASSERT_EQUAL_FLOAT(0, p.signals[c->out_count].value);
    TEST_ASSERT_EQUAL_FLOAT(0, p.signals[c->out].value);
}

/* A feedback wire closes a loop: the logic it comes from runs after the logic it goes
   into, which reads it as the last scan left it. A button starts three blinks: the
   latch runs the blink until the counter, fed back to the latch's reset, is done; the
   counter is reset while the latch is off. */
void test_program_feedback_loop(void)
{
    static const uint8_t cfg[] = {
        MSG_LOGIC_COUNTER, MSG_FLAG_OUTPUT, 0, 4, 10, 0, 3, 0,  0, 5, 0,    /* counter n4: count <- n3, reset <- n5, */
            0, 0, 0, 3,                                                     /*   preset 3 */
        MSG_LOGIC_BLINK, MSG_FLAG_OUTPUT, 0, 3, 11, 0, 2, 0,                /* blink n3 <- n2, */
            0, 0, 0, 10,  0, 0, 0, 10,                                      /*   10 ms on, 10 ms off */
        MSG_UI_BUTTON, 0, 0, 1, 0,                                          /* button n1 */
        MSG_LOGIC_LATCH, MSG_FLAG_OUTPUT, 0, 2, 6,                          /* latch n2: set <- n1, */
            0, 1, 0,  0, 4, MSG_REF_FEEDBACK,                               /*   reset <- n4 done, fed back */
        MSG_LOGIC_NOT, MSG_FLAG_OUTPUT, 0, 5, 3, 0, 2, 0,                   /* NOT n5 <- n2 */
    };
    static hw_program p;
    static prog_logic_state st[PROG_NODES_MAX];
    program_compile(cfg, sizeof cfg, &p);
    TEST_ASSERT_EQUAL_UINT8(4, p.node_count);
    TEST_ASSERT_EQUAL_UINT16(2, p.nodes[0].id);     /* the latch first: it reads the counter fed back */
    TEST_ASSERT_EQUAL_UINT16(3, p.nodes[1].id);
    TEST_ASSERT_EQUAL_UINT16(5, p.nodes[2].id);
    TEST_ASSERT_EQUAL_UINT16(4, p.nodes[3].id);
    TEST_ASSERT_EQUAL_HEX8(0x02, p.nodes[0].feedback);
    TEST_ASSERT_EQUAL_UINT8(program_signal(&p, 4, 0), p.nodes[0].in[1]);

    char text[512];
    program_describe(&p, text, sizeof text);
    TEST_ASSERT_NOT_NULL(strstr(text, "LATCH n2 set s3 reset s0' -> s4"));
    TEST_ASSERT_NOT_NULL(strstr(text, "COUNTER n4 preset 3 count s2 reset s5 -> s0 s1"));

    for (uint8_t i = 0; i < p.node_count; i++) program_logic_start(&p, &p.nodes[i], &st[i]);
    uint8_t button = program_signal(&p, 1, 0), latch = program_signal(&p, 2, 0), blink = program_signal(&p, 3, 0);
    p.signals[button].value = 1;
    scan_logic(&p, st);
    p.signals[button].value = 0;                    /* pressed for one scan */
    int blinks = 0;
    bool was = false;
    for (int i = 0; i < 20; i++) {
        bool on = p.signals[blink].value >= 0.5f;
        blinks += on && !was;
        was = on;
        scan_logic(&p, st);
    }
    TEST_ASSERT_EQUAL_INT(3, blinks);
    TEST_ASSERT_EQUAL_FLOAT(0, p.signals[latch].value);
    TEST_ASSERT_EQUAL_FLOAT(0, p.signals[blink].value);
}

/* A block (MSG_SUB) isn't run: its parts are, and a reference to one of its outputs
   reads what its table says, through blocks wired to blocks. */
void test_program_blocks(void)
{
    static const uint8_t cfg[] = {
        MSG_UI_BUTTON, 0, 0, 1, 0,                                          /* button n1 */
        MSG_SUB, MSG_FLAG_OUTPUT, 0, 2, 11, 1, 0, 10, 1, 1,                 /* block n2: definition 1, parts from n10, */
            0, 1, 0,  0, 10, 0,                                             /*   input <- n1, output = n10 */
        MSG_LOGIC_NOT, MSG_FLAG_OUTPUT | MSG_FLAG_PART, 0, 10, 3, 0, 1, 0,  /* NOT n10 <- n1: n2's part */
        MSG_HW_LED, MSG_FLAG_OUTPUT, 0, 3, 3, 0, 2, 0,                      /* LED n3 <- n2's output */
        MSG_SUB, MSG_FLAG_OUTPUT, 0, 4, 11, 1, 0, 11, 1, 1,                 /* block n4: its input straight to */
            0, 2, 0,  0, 2, 0,                                              /*   its output, n2's output */
        MSG_HW_GPO, MSG_FLAG_OUTPUT, 0, 5, 4, 0, 4, 0, 7,                   /* GPO n5 <- n4's output, GPIO 7 */
        MSG_HW_GPO, MSG_FLAG_OUTPUT, 0, 6, 4, 0, 4, 1, 8,                   /* GPO n6 <- n4's output 1: none */
    };
    static hw_program p;
    program_compile(cfg, sizeof cfg, &p);
    TEST_ASSERT_EQUAL_UINT8(2, p.signal_count);                         /* the button and the NOT: no block */
    uint8_t not = program_signal(&p, 10, 0);
    TEST_ASSERT_NOT_EQUAL(PROG_NO_SIGNAL, not);
    TEST_ASSERT_EQUAL_UINT8(4, p.node_count);
    TEST_ASSERT_EQUAL_UINT8(program_signal(&p, 1, 0), p.nodes[0].in[0]);   /* the NOT reads the button */
    TEST_ASSERT_EQUAL_UINT8(not, p.nodes[1].in[0]);                     /* the LED reads the NOT */
    TEST_ASSERT_EQUAL_UINT8(not, p.nodes[2].in[0]);                     /* the GPO too, through both blocks */
    TEST_ASSERT_EQUAL_UINT8(PROG_NO_SIGNAL, p.nodes[3].in[0]);

    TEST_ASSERT_TRUE(sub_payload_valid(cfg + 10, 11, MSG_FLAG_OUTPUT));
    TEST_ASSERT_FALSE(sub_payload_valid(cfg + 10, 11, 0));              /* without outputs */
    TEST_ASSERT_FALSE(sub_payload_valid(cfg + 10, 10, MSG_FLAG_OUTPUT));
}

/* A block's output fed back is a feedback reference, whatever its table says. */
void test_program_block_feedback(void)
{
    static const uint8_t cfg[] = {
        MSG_SUB, MSG_FLAG_OUTPUT, 0, 2, 8, 1, 0, 10, 0, 1,  0, 10, 0,       /* block n2: no input, output = n10 */
        MSG_LOGIC_TOGGLE, MSG_FLAG_OUTPUT | MSG_FLAG_PART, 0, 10, 3,        /* toggle n10: n2's part, */
            0, 3, 0,                                                        /*   <- n3 */
        MSG_LOGIC_NOT, MSG_FLAG_OUTPUT, 0, 3, 3, 0, 2, MSG_REF_FEEDBACK,    /* NOT n3 <- n2, fed back */
    };
    static hw_program p;
    program_compile(cfg, sizeof cfg, &p);
    TEST_ASSERT_EQUAL_UINT8(2, p.node_count);
    TEST_ASSERT_EQUAL_UINT16(3, p.nodes[0].id);                         /* the NOT first: it reads the toggle fed back */
    TEST_ASSERT_EQUAL_HEX8(0x01, p.nodes[0].feedback);
    TEST_ASSERT_EQUAL_UINT8(program_signal(&p, 10, 0), p.nodes[0].in[0]);
}

/* The action words core 0 sends for a command (handle_websocket_msg in src/websockets.c):
 * one per payload byte, for the element's output of the same index. */
static void assert_action_words(const char *name)
{
    const struct fifo_example *words = fifo_example(name);
    config_element_iterator it;
    messages(&it, example(name));

    TEST_ASSERT_TRUE(it.next(&it));
    TEST_ASSERT_EQUAL_UINT(it.len, words->len);
    for (uint8_t i = 0; i < it.len; i++) {
        TEST_ASSERT_EQUAL_HEX32(fifo_action_word(it.id, i, it.pay[i]), words->words[i]);
        TEST_ASSERT_EQUAL_UINT8(FIFO_OP_CONTROL_ACTION, fifo_op(words->words[i]));
        TEST_ASSERT_EQUAL_UINT8(i, fifo_op_output(words->words[i]));
    }
}

void test_fifo_action_word_matches_command_button(void) { assert_action_words("command-button"); }
void test_fifo_action_word_matches_command_slider(void) { assert_action_words("command-slider"); }
void test_fifo_action_words_match_command_slider_2d(void) { assert_action_words("command-slider-2d"); }

void test_fifo_words_decode(void)
{
    uint32_t action = fifo_action_word(0x1234, FIFO_OUTPUTS_MAX - 1, VALUE_HALF);
    TEST_ASSERT_EQUAL_UINT8(FIFO_OP_CONTROL_ACTION, fifo_op(action));
    TEST_ASSERT_EQUAL_UINT8(FIFO_OUTPUTS_MAX - 1, fifo_op_output(action));
    TEST_ASSERT_EQUAL_HEX16(0x1234, fifo_op_id(action));
    TEST_ASSERT_EQUAL_UINT8(VALUE_HALF, fifo_op_value(action));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_every_packet_example_is_well_formed);
    RUN_TEST(test_device_id);
    RUN_TEST(test_command_button);
    RUN_TEST(test_command_slider);
    RUN_TEST(test_command_slider_2d);
    RUN_TEST(test_update_examples);
    RUN_TEST(test_default_config);
    RUN_TEST(test_save_config_keeps_device_config_then_appends_graph);
    RUN_TEST(test_graph_messages_belong_to_controls);
    RUN_TEST(test_wires_join_matching_types);
    RUN_TEST(test_element_type_bits);
    RUN_TEST(test_pin_usable);
    RUN_TEST(test_update_ap_address);
    RUN_TEST(test_net_address_usable);
    RUN_TEST(test_keyboard_config);
    RUN_TEST(test_keyboard_payload_valid);
    RUN_TEST(test_slider_2d_config);
    RUN_TEST(test_legacy_config);
    RUN_TEST(test_light_config);
    RUN_TEST(test_gpi_config);
    RUN_TEST(test_program_examples_match_their_configs);
    RUN_TEST(test_every_config_example_with_hardware_has_a_program);
    RUN_TEST(test_program_signals);
    RUN_TEST(test_program_limits);
    RUN_TEST(test_logic_config);
    RUN_TEST(test_logic_payload_valid);
    RUN_TEST(test_program_orders_logic);
    RUN_TEST(test_program_logic_step);
    RUN_TEST(test_program_logic_more);
    RUN_TEST(test_program_pwm_full_scale_through_logic);
    RUN_TEST(test_counter_payload_valid);
    RUN_TEST(test_stepper_payload_read);
    RUN_TEST(test_stepper_motion_of);
    RUN_TEST(test_stepper_moves_to_its_target);
    RUN_TEST(test_stepper_turns_at_a_speed);
    RUN_TEST(test_stepper_coils);
    RUN_TEST(test_servo_payload_read);
    RUN_TEST(test_servo_angle_and_pulse);
    RUN_TEST(test_ds18b20_scratchpad);
    RUN_TEST(test_ds18b20_program);
    RUN_TEST(test_sine);
    RUN_TEST(test_clock);
    RUN_TEST(test_encoder_decode);
    RUN_TEST(test_encoder_program);
    RUN_TEST(test_neopixel_payload_read);
    RUN_TEST(test_neopixel_bar);
    RUN_TEST(test_neopixel_point);
    RUN_TEST(test_neopixel_point_blends);
    RUN_TEST(test_neopixel_patch);
    RUN_TEST(test_neopixel_colour_input_and_switch);
    RUN_TEST(test_neopixel_chain);
    RUN_TEST(test_feedback_reference);
    RUN_TEST(test_program_counter);
    RUN_TEST(test_program_feedback_loop);
    RUN_TEST(test_program_blocks);
    RUN_TEST(test_program_block_feedback);
    RUN_TEST(test_fifo_action_word_matches_command_button);
    RUN_TEST(test_fifo_action_word_matches_command_slider);
    RUN_TEST(test_fifo_action_words_match_command_slider_2d);
    RUN_TEST(test_fifo_words_decode);

    return UNITY_END();
}
