/*
 * Checks the examples in PROTOCOL.md against the protocol constants and helpers.
 * tests/protocol_examples.h is generated from PROTOCOL.md by make/examples.awk.
 */

#include "unity.h"
#include "protocol.h"
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
                if (controls.type > 0x3F || controls.id != labels.id) continue;
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
        if (it.type <= 0x3F && it.id == id) return it.type;
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
 * driven by a button, a PWM by sliders, 2D sliders, ADCs or the core temperature. */
void test_wires_join_matching_types(void)
{
    for (size_t i = 0; i < PROTOCOL_EXAMPLE_COUNT; i++) {
        const struct protocol_example *ex = &protocol_examples[i];
        if (ex->bytes[0] != PKT_CONFIG) continue;

        config_element_iterator it;
        messages(&it, ex);
        while (it.next(&it)) {
            if (it.type == MSG_HW_LED || it.type == MSG_HW_GPO || it.type == MSG_HW_KEYBOARD || it.type == MSG_UI_INDICATOR) {
                msg_ref src = msg_ref_at(it.pay, it.flags);
                /* A boolean source: a button or a GPIO input. */
                if (src.id) TEST_ASSERT_TRUE_MESSAGE(element_type(ex, src.id) == MSG_UI_BUTTON
                                                     || element_type(ex, src.id) == MSG_HW_GPI, ex->name);
                TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, src.output, ex->name);
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
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_GPI, fifo_op_type(fifo_op_word(FIFO_OP_SETUP, MSG_HW_GPI, 1, 3)));
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

    /* The state core 1 reports for it. */
    uint32_t word = fifo_gpi_word(28, 1);
    TEST_ASSERT_EQUAL_UINT8(28, fifo_gpi_pin(word));
    TEST_ASSERT_EQUAL_INT(1, fifo_gpi_on(word));
    TEST_ASSERT_EQUAL_INT(0, fifo_gpi_on(fifo_gpi_word(28, 0)));
    TEST_ASSERT_EQUAL_HEX8(0x05, fifo_gpi_options(fifo_gpi_options_word(0x05)));
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

/* The words core 0 sends for a config payload (unmarshal_controls in src/config.c): a clean
 * setup, the setup words, then the action words for each 2D slider's home. */
static void assert_setup_words(const char *name)
{
    const struct fifo_example *words = fifo_example(name);
    config_element_iterator it;
    messages(&it, example(name));

    TEST_ASSERT_TRUE(words->len >= 1);
    TEST_ASSERT_EQUAL_HEX32(fifo_op_word(FIFO_OP_CLEAN_SETUP, 0, 0, 0), words->words[0]);

    size_t n = 1;
    while (it.next(&it)) {
        uint8_t pin;
        pwm_payload pwm;
        switch (it.type) {
        case MSG_HW_LED: pin = 0; break;
        case MSG_HW_KEYBOARD: pin = 0; break;   /* core 0 keeps the keystroke */
        case MSG_HW_GPO: pin = it.pay[msg_ref_len(it.flags)]; break;
        case MSG_HW_PWM:
            TEST_ASSERT_TRUE(pwm_payload_read(it.pay, it.len, it.flags, &pwm));
            pin = pwm.pin;
            break;
        case MSG_HW_ADC:
            /* The channel, and no reference word: an ADC is a source. */
            TEST_ASSERT_TRUE(n + 1 <= words->len);
            TEST_ASSERT_EQUAL_HEX32(fifo_op_word(FIFO_OP_SETUP, it.type, it.id, it.pay[0]), words->words[n++]);
            continue;
        case MSG_HW_CORE:
            /* Nothing in the low byte and nothing after: the chip's sensor is a source too. */
            TEST_ASSERT_TRUE(n + 1 <= words->len);
            TEST_ASSERT_EQUAL_HEX32(fifo_op_word(FIFO_OP_SETUP, it.type, it.id, 0), words->words[n++]);
            continue;
        case MSG_HW_GPI:
            /* The pin, then its options: a GPIO input is a source too. */
            TEST_ASSERT_TRUE(n + 2 <= words->len);
            TEST_ASSERT_EQUAL_HEX32(fifo_op_word(FIFO_OP_SETUP, it.type, it.id, it.pay[0]), words->words[n++]);
            TEST_ASSERT_EQUAL_HEX32(fifo_gpi_options_word(it.pay[1]), words->words[n++]);
            continue;
        default: continue;
        }
        TEST_ASSERT_TRUE(n + 2 <= words->len);
        TEST_ASSERT_EQUAL_HEX32(fifo_op_word(FIFO_OP_SETUP, it.type, it.id, pin), words->words[n++]);
        TEST_ASSERT_EQUAL_HEX32(fifo_ref_word(msg_ref_at(it.pay, it.flags)), words->words[n++]);
        if (it.type == MSG_HW_PWM) {
            TEST_ASSERT_TRUE(n + 1 <= words->len);
            TEST_ASSERT_EQUAL_HEX32(fifo_pwm_word(pwm.freq, pwm.options), words->words[n++]);
            TEST_ASSERT_TRUE(n + 3 <= words->len);
            TEST_ASSERT_EQUAL_HEX32(fifo_duty_word(pwm.duty, pwm.duty_min, pwm.duty_max), words->words[n++]);
            TEST_ASSERT_EQUAL_HEX32(fifo_ref_word(pwm.freq_source), words->words[n++]);
            TEST_ASSERT_EQUAL_HEX32(fifo_freq_word(pwm.freq_min, pwm.freq_max), words->words[n++]);
        }
    }
    messages(&it, example(name));
    while (it.next(&it)) {
        if (it.type != MSG_UI_SLIDER_2D) continue;
        uint8_t home[2];
        slider_2d_home(it.pay, it.len, home);
        TEST_ASSERT_TRUE(n + 2 <= words->len);
        TEST_ASSERT_EQUAL_HEX32(fifo_action_word(it.id, 0, home[0]), words->words[n++]);
        TEST_ASSERT_EQUAL_HEX32(fifo_action_word(it.id, 1, home[1]), words->words[n++]);
    }
    TEST_ASSERT_EQUAL_UINT(words->len, n);
}

void test_fifo_setup_words_match_save_config(void) { assert_setup_words("save-config"); }
void test_fifo_setup_words_match_adc_config(void) { assert_setup_words("adc-config"); }
void test_fifo_setup_words_match_keyboard_config(void) { assert_setup_words("keyboard-config"); }
void test_fifo_setup_words_match_slider_2d_config(void) { assert_setup_words("slider-2d-config"); }
void test_fifo_setup_words_match_legacy_config(void) { assert_setup_words("legacy-config"); }
void test_fifo_setup_words_match_light_config(void) { assert_setup_words("light-config"); }
void test_fifo_setup_words_match_gpi_config(void) { assert_setup_words("gpi-config"); }
void test_fifo_setup_words_match_core_config(void) { assert_setup_words("core-config"); }

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
    uint32_t setup = fifo_op_word(FIFO_OP_SETUP, MSG_HW_GPO, 0x1234, 28);
    TEST_ASSERT_EQUAL_UINT8(FIFO_OP_SETUP, fifo_op(setup));
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_GPO, fifo_op_type(setup));
    TEST_ASSERT_EQUAL_HEX16(0x1234, fifo_op_id(setup));
    TEST_ASSERT_EQUAL_UINT8(28, fifo_op_value(setup));
    const msg_ref ref = { 0xBEEF, 1 };
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, fifo_ref_source(fifo_ref_word(ref)));
    TEST_ASSERT_EQUAL_UINT8(1, fifo_ref_output(fifo_ref_word(ref)));

    uint32_t action = fifo_action_word(0x1234, FIFO_OUTPUTS_MAX - 1, VALUE_HALF);
    TEST_ASSERT_EQUAL_UINT8(FIFO_OP_CONTROL_ACTION, fifo_op(action));
    TEST_ASSERT_EQUAL_UINT8(FIFO_OUTPUTS_MAX - 1, fifo_op_output(action));
    TEST_ASSERT_EQUAL_HEX16(0x1234, fifo_op_id(action));
    TEST_ASSERT_EQUAL_UINT8(VALUE_HALF, fifo_op_value(action));

    uint32_t pwm = fifo_pwm_word(65535, PWM_DUTY_INPUT);
    TEST_ASSERT_EQUAL_UINT16(65535, fifo_pwm_freq(pwm));
    TEST_ASSERT_EQUAL_HEX8(PWM_DUTY_INPUT, fifo_pwm_options(pwm));

    uint32_t duty = fifo_duty_word(50, 80, 20);
    TEST_ASSERT_EQUAL_UINT8(50, fifo_duty_const(duty));
    TEST_ASSERT_EQUAL_UINT8(80, fifo_duty_min(duty));
    TEST_ASSERT_EQUAL_UINT8(20, fifo_duty_max(duty));

    uint32_t freq = fifo_freq_word(PWM_FREQ_MIN, 65535);
    TEST_ASSERT_EQUAL_UINT16(PWM_FREQ_MIN, fifo_freq_min(freq));
    TEST_ASSERT_EQUAL_UINT16(65535, fifo_freq_max(freq));

    uint32_t adc = fifo_adc_word(ADC_CHANNELS - 1, ADC_RAW_FULL);
    TEST_ASSERT_EQUAL_UINT8(ADC_CHANNELS - 1, fifo_adc_channel(adc));
    TEST_ASSERT_EQUAL_UINT16(ADC_RAW_FULL, fifo_adc_raw(adc));
}

/* PROTOCOL.md, Part 3: only 5 type bits travel in a setup word, and core 1 forces bit 5. */
void test_fifo_setup_type_arrives_as_hw_type(void)
{
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_ADC, fifo_op_type(fifo_op_word(FIFO_OP_SETUP, MSG_HW_ADC, 1, 0)));
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_KEYBOARD, fifo_op_type(fifo_op_word(FIFO_OP_SETUP, MSG_HW_KEYBOARD, 1, 0)));
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_CORE, fifo_op_type(fifo_op_word(FIFO_OP_SETUP, MSG_HW_CORE, 1, 0)));
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
    RUN_TEST(test_fifo_setup_words_match_save_config);
    RUN_TEST(test_fifo_setup_words_match_adc_config);
    RUN_TEST(test_fifo_setup_words_match_keyboard_config);
    RUN_TEST(test_fifo_setup_words_match_slider_2d_config);
    RUN_TEST(test_fifo_setup_words_match_legacy_config);
    RUN_TEST(test_fifo_setup_words_match_light_config);
    RUN_TEST(test_fifo_setup_words_match_gpi_config);
    RUN_TEST(test_fifo_setup_words_match_core_config);
    RUN_TEST(test_fifo_action_word_matches_command_button);
    RUN_TEST(test_fifo_action_word_matches_command_slider);
    RUN_TEST(test_fifo_action_words_match_command_slider_2d);
    RUN_TEST(test_fifo_words_decode);
    RUN_TEST(test_fifo_setup_type_arrives_as_hw_type);

    return UNITY_END();
}
