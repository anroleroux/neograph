#include "unity.h"
#include "config/config_iterator.h"

/* Optional: Unity setup/teardown */
void setUp(void) {}
void tearDown(void) {}

void test_config_iterator_message_exist(void)
{
    uint8_t buffer1[] = {0x01, 0x00, 0x00, 0x01, 0x01, 0xAA};
    config_element_iterator it1;
    config_element_iterator_init(&it1, buffer1, sizeof(buffer1));
    TEST_ASSERT_TRUE(it1.has_next(&it1));

    uint8_t buffer2[] = {};
    config_element_iterator it2;
    config_element_iterator_init(&it2, buffer2, sizeof(buffer2));
    TEST_ASSERT_FALSE(it2.has_next(&it2));

    uint8_t buffer3[] = {0x01, 0x00, 0x00, 0x01, 0x02, 0xAA};
    config_element_iterator it3;
    config_element_iterator_init(&it3, buffer3, sizeof(buffer3));
    TEST_ASSERT_FALSE(it3.has_next(&it3));
}

void test_config_iterator_detects_message(void)
{
    uint8_t buffer[] = {
        0x01, 0x00, 0x00, 0x01, 0x02, 0xAA, 0xBB
    };

    config_element_iterator it;

    config_element_iterator_init(&it, buffer, sizeof(buffer));

    TEST_ASSERT_TRUE(it.next(&it));

    TEST_ASSERT_EQUAL_UINT8 (0x01, it.type);
    TEST_ASSERT_EQUAL_UINT8 (0x0001, it.id);
    TEST_ASSERT_EQUAL_UINT8 (2, it.len);
    TEST_ASSERT_EQUAL_UINT8 (0xAA, it.pay[0]);
    TEST_ASSERT_EQUAL_UINT32(7, it.pos);

    TEST_ASSERT_FALSE(it.next(&it));
}

void test_config_iterator_multi_message(void)
{
    uint8_t buffer[] = {
        0x01, 0x00, 0x00, 0x01, 0x02, 0xAA, 0xBB,
        0x02, 0x00, 0x00, 0x02, 0x01, 0xCC
    };

    config_element_iterator it;

    config_element_iterator_init(&it, buffer, sizeof(buffer));

    TEST_ASSERT_TRUE(it.next(&it));
    TEST_ASSERT_TRUE(it.next(&it));
    TEST_ASSERT_FALSE(it.next(&it));
}

void test_config_iterator_reset(void)
{
    uint8_t buffer[] = {
        0x01, 0x00, 0x00, 0x01, 0x02, 0xAA, 0xBB
    };

    config_element_iterator it;

    config_element_iterator_init(&it, buffer, sizeof(buffer));

    TEST_ASSERT_TRUE(it.next(&it));
    TEST_ASSERT_FALSE(it.next(&it));
    it.reset(&it);
    TEST_ASSERT_TRUE(it.next(&it));
    TEST_ASSERT_FALSE(it.next(&it));
}

/* A stored config: network 0 name "AB", SSID "XY". */
static const uint8_t stored[] = {
    0x0A, 0x00, 0x16,
    0xB3, 0x00, 0x00, 0x00, 0x03, 0x01, 'A', 'B',
    0xB3, 0x00, 0x00, 0x00, 0x03, 0x02, 'X', 'Y',
    0xB2, 0x00, 0x00, 0x00, 0x01, 0x00,
};

void test_replace_field_same_length(void)
{
    const uint8_t msg[] = {0xB3, 0x00, 0x00, 0x00, 0x03, 0x02, 'Q', 'R'};
    uint8_t out[64];
    size_t n = config_replace_field(stored, sizeof stored, msg, sizeof msg, out, sizeof out);
    TEST_ASSERT_EQUAL_UINT(sizeof stored, n);
    const uint8_t want[] = {
        0x0A, 0x00, 0x16,
        0xB3, 0x00, 0x00, 0x00, 0x03, 0x01, 'A', 'B',
        0xB3, 0x00, 0x00, 0x00, 0x03, 0x02, 'Q', 'R',
        0xB2, 0x00, 0x00, 0x00, 0x01, 0x00,
    };
    TEST_ASSERT_EQUAL_MEMORY(want, out, sizeof want);
}

void test_replace_field_longer_and_shorter(void)
{
    uint8_t out[64];

    const uint8_t longer[] = {0xB3, 0x00, 0x00, 0x00, 0x05, 0x01, 'L', 'O', 'N', 'G'};
    size_t n = config_replace_field(stored, sizeof stored, longer, sizeof longer, out, sizeof out);
    TEST_ASSERT_EQUAL_UINT(sizeof stored + 2, n);
    TEST_ASSERT_EQUAL_HEX8(0x18, out[2]);                         /* length header updated */
    TEST_ASSERT_EQUAL_MEMORY(longer, out + 3, sizeof longer);     /* in place of the name */
    TEST_ASSERT_EQUAL_MEMORY(stored + 11, out + 13, 14);          /* the rest follows */

    const uint8_t shorter[] = {0xB2, 0x00, 0x00, 0x00, 0x01, 0x00};
    n = config_replace_field(stored, sizeof stored, shorter, sizeof shorter, out, sizeof out);
    TEST_ASSERT_EQUAL_UINT(sizeof stored, n);                     /* last message, same size */
}

void test_replace_field_rejects(void)
{
    uint8_t out[64];

    /* no stored message with that field */
    const uint8_t password[] = {0xB3, 0x00, 0x00, 0x00, 0x03, 0x03, 'p', 'w'};
    TEST_ASSERT_EQUAL_UINT(0, config_replace_field(stored, sizeof stored, password, sizeof password, out, sizeof out));

    /* other element ID */
    const uint8_t other[] = {0xB3, 0x00, 0x00, 0x01, 0x03, 0x01, 'A', 'B'};
    TEST_ASSERT_EQUAL_UINT(0, config_replace_field(stored, sizeof stored, other, sizeof other, out, sizeof out));

    /* message length says more than was sent */
    const uint8_t cut[] = {0xB3, 0x00, 0x00, 0x00, 0x09, 0x01, 'A'};
    TEST_ASSERT_EQUAL_UINT(0, config_replace_field(stored, sizeof stored, cut, sizeof cut, out, sizeof out));

    /* result does not fit the output */
    const uint8_t longer[] = {0xB3, 0x00, 0x00, 0x00, 0x05, 0x01, 'L', 'O', 'N', 'G'};
    TEST_ASSERT_EQUAL_UINT(0, config_replace_field(stored, sizeof stored, longer, sizeof longer, out, sizeof stored + 1));
}

/* config_set_field replaces a field as config_replace_field does, and adds one the
   element doesn't have after the element's last message. */
void test_set_field_replaces(void)
{
    const uint8_t msg[] = {0xB3, 0x00, 0x00, 0x00, 0x03, 0x02, 'Q', 'R'};
    uint8_t replaced[64], set[64];
    size_t n = config_replace_field(stored, sizeof stored, msg, sizeof msg, replaced, sizeof replaced);
    TEST_ASSERT_EQUAL_UINT(n, config_set_field(stored, sizeof stored, msg, sizeof msg, set, sizeof set));
    TEST_ASSERT_EQUAL_MEMORY(replaced, set, n);
}

void test_set_field_adds(void)
{
    uint8_t out[64];

    /* network 0's address: after its last message, the B2 */
    const uint8_t address[] = {0xB3, 0x00, 0x00, 0x00, 0x05, 0x04, 10, 0, 0, 1};
    size_t n = config_set_field(stored, sizeof stored, address, sizeof address, out, sizeof out);
    TEST_ASSERT_EQUAL_UINT(sizeof stored + sizeof address, n);
    TEST_ASSERT_EQUAL_HEX8(0x16 + sizeof address, out[2]);           /* length header updated */
    TEST_ASSERT_EQUAL_MEMORY(stored + 3, out + 3, sizeof stored - 3);
    TEST_ASSERT_EQUAL_MEMORY(address, out + sizeof stored, sizeof address);

    /* a device field, after network fields of another element type: B1 joins B0 */
    const uint8_t with_device[] = {
        0x0A, 0x00, 0x14,
        0xB0, 0x00, 0x00, 0x00, 0x01, 0x00,
        0xB3, 0x00, 0x00, 0x00, 0x03, 0x01, 'A', 'B',
        0xB2, 0x00, 0x00, 0x00, 0x01, 0x00,
    };
    const uint8_t name[] = {0xB1, 0x00, 0x00, 0x00, 0x02, 0x01, 'N'};
    n = config_set_field(with_device, sizeof with_device, name, sizeof name, out, sizeof out);
    TEST_ASSERT_EQUAL_UINT(sizeof with_device + sizeof name, n);
    TEST_ASSERT_EQUAL_MEMORY(with_device + 3, out + 3, 6);           /* the B0 */
    TEST_ASSERT_EQUAL_MEMORY(name, out + 9, sizeof name);            /* then the new B1 */
    TEST_ASSERT_EQUAL_MEMORY(with_device + 9, out + 9 + sizeof name, sizeof with_device - 9);
}

void test_set_field_rejects(void)
{
    uint8_t out[64];

    /* no such network */
    const uint8_t other[] = {0xB3, 0x00, 0x00, 0x01, 0x05, 0x04, 10, 0, 0, 1};
    TEST_ASSERT_EQUAL_UINT(0, config_set_field(stored, sizeof stored, other, sizeof other, out, sizeof out));

    /* not a field message: B2 declares the network, it isn't added */
    const uint8_t declare[] = {0xB2, 0x00, 0x00, 0x00, 0x02, 0x07, 0x01};
    TEST_ASSERT_EQUAL_UINT(0, config_set_field(stored, sizeof stored, declare, sizeof declare, out, sizeof out));

    /* a control element: 21 | 1 is 21, 20 | 1 is 21, but neither is a field */
    const uint8_t control[] = {0x21, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x01};
    TEST_ASSERT_EQUAL_UINT(0, config_set_field(stored, sizeof stored, control, sizeof control, out, sizeof out));

    /* the result does not fit */
    const uint8_t address[] = {0xB3, 0x00, 0x00, 0x00, 0x05, 0x04, 10, 0, 0, 1};
    TEST_ASSERT_EQUAL_UINT(0, config_set_field(stored, sizeof stored, address, sizeof address, out, sizeof stored + 9));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_config_iterator_message_exist);
    RUN_TEST(test_config_iterator_detects_message);
    RUN_TEST(test_config_iterator_multi_message);
    RUN_TEST(test_config_iterator_reset);
    RUN_TEST(test_replace_field_same_length);
    RUN_TEST(test_replace_field_longer_and_shorter);
    RUN_TEST(test_replace_field_rejects);
    RUN_TEST(test_set_field_replaces);
    RUN_TEST(test_set_field_adds);
    RUN_TEST(test_set_field_rejects);

    return UNITY_END();
}
