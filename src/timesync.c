#include <stdbool.h>
#include <stdio.h>

#include "pico/cyw43_arch.h"
#include "lwip/apps/sntp.h"

#include "clock.h"
#include "config/config_iterator.h"
#include "hwman.h"
#include "protocol.h"
#include "timesync.h"

static enum { NO_CLOCK, OFF, OFFLINE, WAITING, SYNCED } state = NO_CLOCK;
static int32_t zone = 0;                // minutes ahead of UTC
static volatile bool got = false;       // an answer, for timesync_task
static volatile uint32_t got_s;

void timesync_got(unsigned long unix_s) {
    got_s = (uint32_t)unix_s;
    got = true;
}

// Ask the time server, on a network; there is no internet otherwise.
static void start(void) {
    if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
        printf("internet time: not on a network\n");
        state = OFFLINE;
        return;
    }
    got = false;
    if (!sntp_enabled()) {
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, TIMESYNC_SERVER);
        sntp_init();
    }
    printf("internet time: asking %s\n", TIMESYNC_SERVER);
    state = WAITING;
}

static void stop(void) {
    if (sntp_enabled()) sntp_stop();
    got = false;
}

void timesync_config(const uint8_t *data, size_t len) {
    config_element_iterator itr;
    config_element_iterator_init(&itr, data, len);
    bool clock = false;
    uint8_t options = 0;
    int16_t z = 0;
    while (!clock && itr.next(&itr))
        clock = itr.type == MSG_HW_CLOCK && clock_payload_read(itr.pay, itr.len, &options, &z);
    zone = z;
    clock_set_zone(zone);

    if (!clock || !(options & CLOCK_SYNC)) {
        stop();
        state = clock ? OFF : NO_CLOCK;
        return;
    }
    if (state == WAITING || state == SYNCED) return;    // on already: not asked again until startup
    start();
}

void timesync_task(void) {
    if (!got) return;
    got = false;
    if (state != WAITING) return;
    uint32_t ms = clock_from_unix(got_s, zone);
    clock_set_ms(ms);
    sntp_stop();            // once: from here on the device keeps the time
    state = SYNCED;
    printf("internet time: clock set to %02lu:%02lu:%02lu\n", (unsigned long)(ms / 3600000),
           (unsigned long)(ms / 60000 % 60), (unsigned long)(ms / 1000 % 60));
}

const char *timesync_status(void) {
    static const char *const names[] = { NULL, "off", "offline", "waiting", "synced" };
    return names[state];
}
