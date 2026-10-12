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

#endif