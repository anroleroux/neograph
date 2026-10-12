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

static void check_config(const uint8_t *p, size_t len) {
    if (len > PKT_CONFIG_MAX) fail("config is bigger than PKT_CONFIG_MAX");

    config_element_iterator it, ctl;
    config_element_iterator_init(&it, p + 3, len - 3);
    bool device = false;
    while (it.next(&it)) {
        switch (it.type) {
        case MSG_DEVICE:
            device = true;
            break;
        /* unmarshal_controls only sets up outputs with exactly these lengths */
        case MSG_HW_LED:
            if (it.len != 2) fail("LED message is not 2 bytes; the device would skip it");
            break;
        case MSG_HW_GPO:
            if (it.len != 3) fail("GPO message is not 3 bytes; the device would skip it");
            else if (it.pay[2] > 28) fail("GPO pin is over 28");
            break;
        case MSG_STRING:
        case MSG_LAYOUT:
        case MSG_WIRE: {
            if (it.type == MSG_STRING ? it.len == 0 : it.len != 4) fail("label, position or wire has the wrong length");
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
        /* handle_packet sends pay[0], then the rest in 4-byte words */
        if (it.len < 1 || (it.len - 1) % 4 != 0) fail("command payload is not 1 + 4n bytes");
    }
}

static void check_update(const uint8_t *p, size_t len, bool secret) {
    config_element_iterator it;
    config_element_iterator_init(&it, p + 3, len - 3);
    int n = 0;
    while (it.next(&it)) n++;
    if (n != 1) fail("field update does not hold exactly one message");

    const uint8_t *target = secret ? stored_secrets : stored;
    size_t target_len = secret ? sizeof stored_secrets : stored_len;
    if (!secret && stored_len == 0) {
        fail("field update before any config was sent");
        return;
    }
    /* what update_config / update_secrets would write */
    uint8_t out[PKT_CONFIG_MAX];
    size_t region = secret ? SECRETS_REGION : PKT_CONFIG_MAX;
    if (config_replace_field(target, target_len, p + 3, len - 3, out, region) == 0)
        fail("field update matches no stored field; the device would ignore it");
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
            if (it.pay[-4] != 0x00) fail("a message's reserved byte is not 0");
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
