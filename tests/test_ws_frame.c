#include "unity.h"
#include "networking/ws_frame.h"

void setUp(void) {}
void tearDown(void) {}

#define MAX 4110

void test_needs_a_whole_header(void)
{
    ws_frame f;
    uint8_t one[] = {0x82};
    TEST_ASSERT_EQUAL(WS_FRAME_INCOMPLETE, ws_parse_frame(one, 0, MAX, &f));
    TEST_ASSERT_EQUAL(WS_FRAME_INCOMPLETE, ws_parse_frame(one, 1, MAX, &f));

    /* 16-bit length, but the length bytes have not arrived */
    uint8_t ext[] = {0x82, 0xFE, 0x10};
    TEST_ASSERT_EQUAL(WS_FRAME_INCOMPLETE, ws_parse_frame(ext, sizeof ext, MAX, &f));

    /* masked, but the mask has not arrived */
    uint8_t mask[] = {0x82, 0x83, 0x01, 0x02};
    TEST_ASSERT_EQUAL(WS_FRAME_INCOMPLETE, ws_parse_frame(mask, sizeof mask, MAX, &f));
}

void test_small_masked_frame(void)
{
    /* "Hi" masked with 01 02 03 04, as a browser sends it */
    uint8_t buf[] = {0x81, 0x82, 0x01, 0x02, 0x03, 0x04, 'H' ^ 0x01, 'i' ^ 0x02};
    ws_frame f;
    TEST_ASSERT_EQUAL(WS_FRAME_OK, ws_parse_frame(buf, sizeof buf, MAX, &f));
    TEST_ASSERT_TRUE(f.fin);
    TEST_ASSERT_EQUAL_UINT8(WS_OP_TEXT, f.opcode);
    TEST_ASSERT_TRUE(f.masked);
    TEST_ASSERT_EQUAL_UINT(6, f.header_len);
    TEST_ASSERT_EQUAL_UINT(2, f.payload_len);

    ws_unmask(buf + f.header_len, f.payload_len, f.mask);
    TEST_ASSERT_EQUAL_MEMORY("Hi", buf + f.header_len, 2);

    /* one byte short of the payload */
    TEST_ASSERT_EQUAL(WS_FRAME_INCOMPLETE, ws_parse_frame(buf, sizeof buf - 1, MAX, &f));
}

/* A 4 KB config arrives as a 16-bit-length frame, split across TCP segments. */
void test_large_frame_in_pieces(void)
{
    static uint8_t buf[8 + 4096];
    buf[0] = 0x82; buf[1] = 0xFE; buf[2] = 0x10; buf[3] = 0x00;    /* binary, masked, 4096 */
    buf[4] = buf[5] = buf[6] = buf[7] = 0;

    ws_frame f;
    TEST_ASSERT_EQUAL(WS_FRAME_INCOMPLETE, ws_parse_frame(buf, 1460, MAX, &f));
    TEST_ASSERT_EQUAL(WS_FRAME_INCOMPLETE, ws_parse_frame(buf, 2920, MAX, &f));
    TEST_ASSERT_EQUAL(WS_FRAME_OK, ws_parse_frame(buf, sizeof buf, MAX, &f));
    TEST_ASSERT_EQUAL_UINT(8, f.header_len);
    TEST_ASSERT_EQUAL_UINT(4096, f.payload_len);
}

void test_too_big_is_reported_before_the_payload_arrives(void)
{
    ws_frame f;
    /* 16-bit length 5000, masked: 8 + 5000 > MAX */
    uint8_t big16[] = {0x82, 0xFE, 0x13, 0x88, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL(WS_FRAME_TOO_BIG, ws_parse_frame(big16, sizeof big16, MAX, &f));

    /* 64-bit length, even before the mask arrives */
    uint8_t big64[] = {0x82, 0xFF, 0, 0, 0, 1, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL(WS_FRAME_TOO_BIG, ws_parse_frame(big64, sizeof big64, MAX, &f));

    /* exactly MAX fits */
    uint8_t edge[] = {0x82, 0xFE, (MAX - 8) >> 8, (MAX - 8) & 0xFF, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL(WS_FRAME_INCOMPLETE, ws_parse_frame(edge, sizeof edge, MAX, &f));
}

void test_back_to_back_frames(void)
{
    uint8_t buf[] = {
        0x82, 0x81, 0, 0, 0, 0, 0xAA,     /* binary, 1 byte */
        0x89, 0x80, 0, 0, 0, 0,           /* ping, empty */
    };
    ws_frame f;
    TEST_ASSERT_EQUAL(WS_FRAME_OK, ws_parse_frame(buf, sizeof buf, MAX, &f));
    size_t used = f.header_len + f.payload_len;
    TEST_ASSERT_EQUAL_UINT(7, used);

    TEST_ASSERT_EQUAL(WS_FRAME_OK, ws_parse_frame(buf + used, sizeof buf - used, MAX, &f));
    TEST_ASSERT_EQUAL_UINT8(WS_OP_PING, f.opcode);
    TEST_ASSERT_EQUAL_UINT(0, f.payload_len);
}

void test_server_header(void)
{
    uint8_t h[4];
    TEST_ASSERT_EQUAL_UINT(2, ws_frame_header(h, WS_OP_BINARY, 125));
    TEST_ASSERT_EQUAL_HEX8(0x82, h[0]);
    TEST_ASSERT_EQUAL_HEX8(125, h[1]);

    TEST_ASSERT_EQUAL_UINT(4, ws_frame_header(h, WS_OP_BINARY, 4096));
    TEST_ASSERT_EQUAL_HEX8(126, h[1]);
    TEST_ASSERT_EQUAL_HEX8(0x10, h[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, h[3]);

    TEST_ASSERT_EQUAL_UINT(4, ws_frame_header(h, WS_OP_TEXT, 65535));
    TEST_ASSERT_EQUAL_UINT(0, ws_frame_header(h, WS_OP_BINARY, 65536));

    /* A server frame parses back to the same length. */
    uint8_t frame[4 + 300] = {0};
    ws_frame f;
    ws_frame_header(frame, WS_OP_BINARY, 300);
    TEST_ASSERT_EQUAL(WS_FRAME_OK, ws_parse_frame(frame, sizeof frame, MAX, &f));
    TEST_ASSERT_FALSE(f.masked);
    TEST_ASSERT_EQUAL_UINT(300, f.payload_len);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_needs_a_whole_header);
    RUN_TEST(test_small_masked_frame);
    RUN_TEST(test_large_frame_in_pieces);
    RUN_TEST(test_too_big_is_reported_before_the_payload_arrives);
    RUN_TEST(test_back_to_back_frames);
    RUN_TEST(test_server_header);

    return UNITY_END();
}
