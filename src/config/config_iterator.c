// #include <stdio.h>
// #include <stdlib.h>
// #include <stdint.h>
// #include <string.h>
// #include <stdbool.h>
// #include <stddef.h>

#include "config_iterator.h"
#include "protocol.h"

// bool next_config_element(config_element_iterator *elms) {
//     // if (elms->pos >= elms->size) return false;

//     // if (elms->pos == 0 && elms->pay == NULL) {
//     //     if (elms->type == 0x0A) {
//     //         elms->pos = 3;
//     //     }
//     // } else {
//         elms->pos += (elms->pos + 5 + elms->len);
//     // }

//     // if (elms->pos >= elms->size) {
//     //     elms->pay = NULL;
//     //     return true;
//     // }
//     elms->type = elms->buf[elms->pos];
//     elms->id = elms->buf[elms->pos+2]<<8 | elms->buf[elms->pos+3];
//     elms->len = elms->buf[elms->pos+4];
//     // elms->pay = elms->buf+elms->pos+5;
//     // memcpy(elms->pay, elms->buf+elms->pos+5, elms->len);

//     // printf("debug pos:%d, size:%d, type:%X, id:%d, len:%d\n",elms->pos,elms->size,elms->type,elms->id,elms->len);
//     return true;
// }

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

static bool cei_has_next(config_element_iterator *it)
{
    /* Need at least header bytes */
    if (it->pos + 5 > it->size)
        return false;

    uint8_t len = it->buf[it->pos + 4];

    /* Full message must fit */
    return (it->pos + 5 + len) <= it->size;
}

static bool cei_next(config_element_iterator *it)
{
    if (!cei_has_next(it))
        return false;

    const uint8_t *p = &it->buf[it->pos];
    
    // printf("here has next %02X %02X %02X %02X\n",p[0],p[1],p[2],p[3]);
    it->type = p[0];

    // /* Element ID (example: big-endian) */
    it->id = (uint16_t)p[2] << 8 | p[3];

    it->len = p[4];
    it->pay = &p[5];

    /* Advance iterator */
    it->pos += 5 + it->len;

    return true;
}

static void cei_reset(config_element_iterator *it)
{
    it->pos = 0;
}

void config_element_iterator_init(
    config_element_iterator *it,
    const uint8_t *buf,
    size_t size
) {
    it->buf  = buf;
    it->size = size;
    it->pos  = 0;

    it->has_next = cei_has_next;
    it->next     = cei_next;
    it->reset    = cei_reset;
}

size_t config_replace_field(
    const uint8_t *packet, size_t packet_len,
    const uint8_t *msg, size_t msg_len,
    uint8_t *out, size_t out_size
) {
    /* msg: a message header and at least the field ID */
    if (packet_len < 3 || msg_len < 6 || msg[4] == 0 || (size_t)5 + msg[4] > msg_len)
        return 0;
    size_t new_len = 5 + msg[4];
    uint16_t id = (uint16_t)msg[2] << 8 | msg[3];

    config_element_iterator it;
    config_element_iterator_init(&it, packet + 3, packet_len - 3);
    while (it.next(&it)) {
        if (it.type != msg[0] || it.id != id || it.len == 0 || it.pay[0] != msg[5])
            continue;

        size_t end = 3 + it.pos;                 /* it.pos is just past the match */
        size_t start = end - (5 + it.len);
        size_t total = start + new_len + (packet_len - end);
        if (total > out_size || total - 3 > 0xFFFF)
            return 0;

        memcpy(out, packet, start);
        memcpy(out + start, msg, new_len);
        memcpy(out + start + new_len, packet + end, packet_len - end);
        out[1] = ((total - 3) >> 8) & 0xFF;
        out[2] = (total - 3) & 0xFF;
        return total;
    }
    return 0;
}

size_t config_set_field(
    const uint8_t *packet, size_t packet_len,
    const uint8_t *msg, size_t msg_len,
    uint8_t *out, size_t out_size
) {
    size_t replaced = config_replace_field(packet, packet_len, msg, msg_len, out, out_size);
    if (replaced || packet_len < 3 || msg_len < 6 || msg[4] == 0 || (size_t)5 + msg[4] > msg_len)
        return replaced;
    /* Fields are added only as B1 or B3 messages; B0 and B2 declare the element. */
    if (msg[0] != MSG_DEVICE_FIELD && msg[0] != MSG_NETWORK_FIELD)
        return 0;
    size_t new_len = 5 + msg[4];
    uint16_t id = (uint16_t)msg[2] << 8 | msg[3];

    /* The end of the element's last message: B0 and B1, or B2 and B3, differ in bit 0. */
    size_t at = 0;
    config_element_iterator it;
    config_element_iterator_init(&it, packet + 3, packet_len - 3);
    while (it.next(&it))
        if ((it.type | 1) == (msg[0] | 1) && it.id == id) at = 3 + it.pos;
    if (at == 0) return 0;

    size_t total = packet_len + new_len;
    if (total > out_size || total - 3 > 0xFFFF)
        return 0;
    memcpy(out, packet, at);
    memcpy(out + at, msg, new_len);
    memcpy(out + at + new_len, packet + at, packet_len - at);
    out[1] = ((total - 3) >> 8) & 0xFF;
    out[2] = (total - 3) & 0xFF;
    return total;
}

int packet_data_length(const uint8_t *packet, size_t len)
{
    if (len < 3) return -1;
    int data_length = packet[1] << 8 | packet[2];
    return (size_t)3 + data_length > len ? -1 : data_length;
}
