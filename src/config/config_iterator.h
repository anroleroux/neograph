#ifndef CONFIG_ITERATOR_H_INCLUDED
#define CONFIG_ITERATOR_H_INCLUDED

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct config_element_iterator {
    /* Data */
    const uint8_t *buf;
    size_t size;
    size_t pos;
    
    uint8_t  type;
    uint8_t  flags;     /* header byte 1, e.g. MSG_FLAG_OUTPUT */
    uint16_t id;
    uint8_t  len;
    const uint8_t *pay;
    
    /* "Methods" */
    bool (*has_next)(struct config_element_iterator *it);
    bool (*next)(struct config_element_iterator *it);
    void (*reset)(struct config_element_iterator *it);
} config_element_iterator;

void config_element_iterator_init(
    config_element_iterator *it,
    const uint8_t *buf,
    size_t size
);
// bool next_config_element(config_element_iterator *elms);

/*
 * The payload length a received packet declares in its header, if the packet is
 * long enough to hold it; -1 if it is shorter than a header or claims more bytes
 * than it holds. This is the check the device applies to every received packet.
 */
int packet_data_length(const uint8_t *packet, size_t len);

/*
 * Replace one field message in a stored packet (header included). The first message
 * with the same type, element ID and field ID (first payload byte) as msg is
 * replaced by msg's first message, and the result is written to out with its length
 * header updated. Returns the new packet length, or 0 if nothing matched, msg is not
 * a field message, or the result would not fit out_size.
 */
size_t config_replace_field(
    const uint8_t *packet, size_t packet_len,
    const uint8_t *msg, size_t msg_len,
    uint8_t *out, size_t out_size
);

/*
 * As config_replace_field, but a field the element doesn't have yet is added: msg's
 * first message is inserted after the element's last message (a device's B0/B1, a
 * network's B2/B3, with msg's element ID); only a B1 or B3 is added. Returns 0 if
 * the element isn't in the packet at all, msg is not a field message, or the result
 * would not fit out_size.
 */
size_t config_set_field(
    const uint8_t *packet, size_t packet_len,
    const uint8_t *msg, size_t msg_len,
    uint8_t *out, size_t out_size
);

#endif