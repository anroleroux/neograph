/*
 * Checks the examples in PROTOCOL.md against the protocol constants and helpers.
 * tests/protocol_examples.h is generated from PROTOCOL.md by make/examples.awk.
 */

#include "unity.h"
#include "protocol.h"
#include "program.h"
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
            TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, it.flags & ~(MSG_FLAG_OUTPUT | MSG_FLAG_SHOWN), ex->name);   /* unknown flags */
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
                else if (controls.type == MSG_HW_LED || controls.type == MSG_HW_GPO)
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

/* A floating output the device has: a slider's, a 2D slider's X or Y, an ADC's or the core temperature's. */
static void assert_floating_hw(const struct protocol_example *ex, msg_ref src)
{
    if (src.id == 0) return;
    uint8_t type = element_type(ex, src.id);
    TEST_ASSERT_TRUE_MESSAGE(type == MSG_UI_SLIDER || type == MSG_UI_SLIDER_2D || type == MSG_HW_ADC
                             || type == MSG_HW_CORE, ex->name);
    TEST_ASSERT_TRUE_MESSAGE(src.output < (type == MSG_UI_SLIDER_2D ? 2 : 1), ex->name);
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
            bool boolean_in = it.type == MSG_HW_LED || it.type == MSG_HW_GPO || it.type == MSG_HW_KEYBOARD
                || it.type == MSG_UI_INDICATOR || it.type == MSG_LOGIC_AND || it.type == MSG_LOGIC_OR
                || it.type == MSG_LOGIC_NOT || it.type == MSG_LOGIC_DELAY;
            if (boolean_in) {
                /* Boolean sources: a button, a GPIO input or logic. An AND or OR has several. */
                uint8_t r = msg_ref_len(it.flags);
                uint8_t refs = (it.type == MSG_LOGIC_AND || it.type == MSG_LOGIC_OR) ? it.len / r : 1;
                for (uint8_t k = 0; k < refs; k++) {
                    msg_ref src = msg_ref_at(it.pay + k * r, it.flags);
                    uint8_t type = element_type(ex, src.id);
                    if (src.id) TEST_ASSERT_TRUE_MESSAGE(type == MSG_UI_BUTTON || type == MSG_HW_GPI
                                                         || (type & MSG_LOGIC_BIT), ex->name);
                    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, src.output, ex->name);
                }
            } else if (it.type == MSG_LOGIC_COMPARE) {
                assert_floating_hw(ex, msg_ref_at(it.pay, it.flags));
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
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_BIT | MSG_INPUT_BIT, MSG_HW_GPI & (MSG_HW_BIT | MSG_INPUT_BIT));
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_BIT | MSG_INPUT_BIT, MSG_HW_ADC & (MSG_HW_BIT | MSG_INPUT_BIT));
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
static bool step(uint8_t node, float in, bool on, uint32_t *held, uint32_t ms)
{
    step_p.signals[0].value = in;
    return program_logic_step(&step_p, &step_p.nodes[node], on, held, ms);
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
    RUN_TEST(test_fifo_action_word_matches_command_button);
    RUN_TEST(test_fifo_action_word_matches_command_slider);
    RUN_TEST(test_fifo_action_words_match_command_slider_2d);
    RUN_TEST(test_fifo_words_decode);

    return UNITY_END();
}
