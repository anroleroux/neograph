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
            TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, it.pay[-4], ex->name);   /* reserved byte */
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

    assert_message(&it, MSG_HW_LED, 2, 2);
    TEST_ASSERT_EQUAL_UINT16(1, it.pay[0] << 8 | it.pay[1]);       /* source n1 */
    assert_message(&it, MSG_STRING, 2, 12);
    TEST_ASSERT_EQUAL_MEMORY("Pompe \xC3\xA0 eau", it.pay, 12);   /* UTF-8 */
    assert_message(&it, MSG_LAYOUT, 2, 4);
    TEST_ASSERT_EQUAL_INT16(100, (int16_t)(it.pay[0] << 8 | it.pay[1]));
    TEST_ASSERT_EQUAL_INT16(40, (int16_t)(it.pay[2] << 8 | it.pay[3]));
    assert_message(&it, MSG_WIRE, 2, 4);                          /* wire n1 -> n2 */
    TEST_ASSERT_EQUAL_HEX16(0xF562, it.pay[0] << 8 | it.pay[1]);
    TEST_ASSERT_EQUAL_HEX16(0x7562, it.pay[2] << 8 | it.pay[3]);

    assert_message(&it, MSG_HW_GPO, 3, 3);
    TEST_ASSERT_EQUAL_UINT16(1, it.pay[0] << 8 | it.pay[1]);       /* source n1 */
    TEST_ASSERT_EQUAL_UINT8(1, it.pay[2]);                         /* pin 1 */
    assert_message(&it, MSG_STRING, 3, 3);
    TEST_ASSERT_EQUAL_MEMORY("GPO", it.pay, 3);
    assert_message(&it, MSG_LAYOUT, 3, 4);
    TEST_ASSERT_EQUAL_INT16(200, (int16_t)(it.pay[0] << 8 | it.pay[1]));
    TEST_ASSERT_EQUAL_INT16(0, (int16_t)(it.pay[2] << 8 | it.pay[3]));
    assert_message(&it, MSG_WIRE, 3, 4);                          /* wire n1 -> n3 */
    TEST_ASSERT_EQUAL_HEX16(0xFBF0, it.pay[0] << 8 | it.pay[1]);
    TEST_ASSERT_EQUAL_HEX16(0x7BF0, it.pay[2] << 8 | it.pay[3]);

    assert_message(&it, MSG_HW_PWM, 4, 15);
    TEST_ASSERT_EQUAL_UINT16(1, it.pay[0] << 8 | it.pay[1]);       /* duty source n1 */
    TEST_ASSERT_EQUAL_UINT8(2, it.pay[2]);                         /* pin 2 */
    TEST_ASSERT_EQUAL_UINT16(1000, it.pay[3] << 8 | it.pay[4]);    /* 1000 Hz */
    TEST_ASSERT_EQUAL_HEX8(PWM_DUTY_INPUT, it.pay[5]);             /* duty from input, constant frequency */
    TEST_ASSERT_EQUAL_UINT8(50, it.pay[6]);                        /* constant duty 50 % */
    TEST_ASSERT_EQUAL_UINT8(0, it.pay[7]);                         /* duty at 0: 0 % */
    TEST_ASSERT_EQUAL_UINT8(50, it.pay[8]);                        /* duty at 1: 50 % */
    TEST_ASSERT_EQUAL_UINT16(0, it.pay[9] << 8 | it.pay[10]);      /* no frequency source */
    TEST_ASSERT_EQUAL_UINT16(100, it.pay[11] << 8 | it.pay[12]);   /* frequency at 0 */
    TEST_ASSERT_EQUAL_UINT16(10000, it.pay[13] << 8 | it.pay[14]); /* frequency at 1 */
    assert_message(&it, MSG_STRING, 4, 3);
    TEST_ASSERT_EQUAL_MEMORY("PWM", it.pay, 3);
    assert_message(&it, MSG_LAYOUT, 4, 4);
    TEST_ASSERT_EQUAL_INT16(200, (int16_t)(it.pay[0] << 8 | it.pay[1]));
    TEST_ASSERT_EQUAL_INT16(120, (int16_t)(it.pay[2] << 8 | it.pay[3]));
    assert_message(&it, MSG_WIRE, 4, 6);                          /* wire n1 -> n4, with its source */
    TEST_ASSERT_EQUAL_HEX16(0xF07F, it.pay[0] << 8 | it.pay[1]);
    TEST_ASSERT_EQUAL_HEX16(0x707F, it.pay[2] << 8 | it.pay[3]);
    TEST_ASSERT_EQUAL_UINT16(1, it.pay[4] << 8 | it.pay[5]);

    TEST_ASSERT_FALSE(it.next(&it));
}

/* Every label, position and wire in a config example describes a control element in
 * the same config. Positions are exactly 4 bytes. A wire belongs to an output with a
 * source: 4 bytes into an LED or GPO, 6 into a PWM, ending with one of its sources. */
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
                uint16_t src = controls.pay[0] << 8 | controls.pay[1];
                if (labels.type != MSG_WIRE)
                    found = true;
                else if (controls.type == MSG_HW_LED || controls.type == MSG_HW_GPO)
                    found = labels.len == 4 && src != 0;
                else if (controls.type == MSG_HW_PWM) {
                    uint16_t wired = labels.pay[4] << 8 | labels.pay[5];
                    found = labels.len == 6 && wired != 0
                        && (wired == src || wired == (controls.pay[9] << 8 | controls.pay[10]));
                }
            }
            TEST_ASSERT_TRUE_MESSAGE(found, ex->name);
        }
    }
}

/* ── Element type bits ────────────────────────────────────────────────── */

void test_element_type_bits(void)
{
    TEST_ASSERT_EQUAL_HEX8(MSG_INPUT_BIT, MSG_UI_BUTTON & (MSG_HW_BIT | MSG_INPUT_BIT));
    TEST_ASSERT_EQUAL_HEX8(MSG_INPUT_BIT, MSG_UI_SLIDER & (MSG_HW_BIT | MSG_INPUT_BIT));
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_BIT, MSG_HW_LED & (MSG_HW_BIT | MSG_INPUT_BIT));
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_BIT, MSG_HW_GPO & (MSG_HW_BIT | MSG_INPUT_BIT));
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_BIT, MSG_HW_PWM & (MSG_HW_BIT | MSG_INPUT_BIT));
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_BIT | MSG_INPUT_BIT, MSG_HW_GPI & (MSG_HW_BIT | MSG_INPUT_BIT));
    TEST_ASSERT_EQUAL_HEX8(0, MSG_UI_GAUGE & (MSG_HW_BIT | MSG_INPUT_BIT));
}

/* ── FIFO words ───────────────────────────────────────────────────────── */

/* The setup words core 0 sends for a config payload (unmarshal_controls in src/config.c). */
void test_fifo_setup_words_match_save_config(void)
{
    const struct fifo_example *words = fifo_example("save-config");
    config_element_iterator it;
    messages(&it, example("save-config"));

    size_t n = 0;
    while (it.next(&it)) {
        uint8_t pin;
        switch (it.type) {
        case MSG_HW_LED: pin = 0; break;
        case MSG_HW_GPO: pin = it.pay[2]; break;
        case MSG_HW_PWM: pin = it.pay[2]; break;
        default: continue;
        }
        TEST_ASSERT_TRUE(n + 2 <= words->len);
        TEST_ASSERT_EQUAL_HEX32(fifo_op_word(FIFO_OP_SETUP, it.type, it.id, pin), words->words[n++]);
        TEST_ASSERT_EQUAL_HEX32(fifo_ref_word(it.pay[0] << 8 | it.pay[1]), words->words[n++]);
        if (it.type == MSG_HW_PWM) {
            TEST_ASSERT_TRUE(n + 1 <= words->len);
            TEST_ASSERT_EQUAL_HEX32(fifo_pwm_word(it.pay[3] << 8 | it.pay[4], it.pay[5]), words->words[n++]);
            TEST_ASSERT_TRUE(n + 3 <= words->len);
            TEST_ASSERT_EQUAL_HEX32(fifo_duty_word(it.pay[6], it.pay[7], it.pay[8]), words->words[n++]);
            TEST_ASSERT_EQUAL_HEX32(fifo_ref_word(it.pay[9] << 8 | it.pay[10]), words->words[n++]);
            TEST_ASSERT_EQUAL_HEX32(fifo_freq_word(it.pay[11] << 8 | it.pay[12], it.pay[13] << 8 | it.pay[14]),
                                    words->words[n++]);
        }
    }
    TEST_ASSERT_EQUAL_UINT(words->len, n);
}

/* The action words core 0 sends for a command (handle_websocket_msg in src/websockets.c). */
static void assert_action_word(const char *name)
{
    const struct fifo_example *words = fifo_example(name);
    config_element_iterator it;
    messages(&it, example(name));

    TEST_ASSERT_TRUE(it.next(&it));
    TEST_ASSERT_EQUAL_UINT(1, words->len);
    TEST_ASSERT_EQUAL_HEX32(fifo_op_word(FIFO_OP_CONTROL_ACTION, it.type, it.id, it.pay[0]),
                            words->words[0]);
}

void test_fifo_action_word_matches_command_button(void) { assert_action_word("command-button"); }
void test_fifo_action_word_matches_command_slider(void) { assert_action_word("command-slider"); }

void test_fifo_words_decode(void)
{
    uint32_t setup = fifo_op_word(FIFO_OP_SETUP, MSG_HW_GPO, 0x1234, 28);
    TEST_ASSERT_EQUAL_UINT8(FIFO_OP_SETUP, fifo_op(setup));
    TEST_ASSERT_EQUAL_HEX8(MSG_HW_GPO, fifo_op_type(setup));
    TEST_ASSERT_EQUAL_HEX16(0x1234, fifo_op_id(setup));
    TEST_ASSERT_EQUAL_UINT8(28, fifo_op_value(setup));
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, fifo_ref_source(fifo_ref_word(0xBEEF)));

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
}

/* Current behaviour (PROTOCOL.md, Part 3): only 5 type bits travel and core 1 forces bit 5. */
void test_fifo_ui_type_arrives_as_hw_type(void)
{
    uint32_t action = fifo_op_word(FIFO_OP_CONTROL_ACTION, MSG_UI_BUTTON, 1, 1);
    TEST_ASSERT_EQUAL_HEX8(MSG_UI_BUTTON | MSG_HW_BIT, fifo_op_type(action));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_every_packet_example_is_well_formed);
    RUN_TEST(test_device_id);
    RUN_TEST(test_command_button);
    RUN_TEST(test_command_slider);
    RUN_TEST(test_update_examples);
    RUN_TEST(test_default_config);
    RUN_TEST(test_save_config_keeps_device_config_then_appends_graph);
    RUN_TEST(test_graph_messages_belong_to_controls);
    RUN_TEST(test_element_type_bits);
    RUN_TEST(test_fifo_setup_words_match_save_config);
    RUN_TEST(test_fifo_action_word_matches_command_button);
    RUN_TEST(test_fifo_action_word_matches_command_slider);
    RUN_TEST(test_fifo_words_decode);
    RUN_TEST(test_fifo_ui_type_arrives_as_hw_type);

    return UNITY_END();
}
