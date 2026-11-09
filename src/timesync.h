#ifndef TIMESYNC_H_INCLUDED
#define TIMESYNC_H_INCLUDED

#include <stddef.h>
#include <stdint.h>

/*
 * Internet time for the device's clock (MSG_HW_CLOCK, CLOCK_SYNC): core 0 asks an
 * internet time server (SNTP, TIMESYNC_SERVER) for the time once, at startup and when a
 * config turns internet time on, if the device is on a network (station mode). It asks
 * again until the server answers, then stops. PROTOCOL.md, "Clocks", describes it.
 */

#define TIMESYNC_SERVER     "pool.ntp.org"

// A config was applied (at startup, or saved): take its first clock's settings, the
// time zone at once, and ask for the time if internet time has just come on.
void timesync_config(const uint8_t *data, size_t len);

// Core 0's main loop: set the clock from an answer, and stop asking.
void timesync_task(void);

// What it is doing, for the browser ("clock <status>" telemetry): "off", "offline" (not on
// a network: the device is its own access point, or didn't join one), "waiting" or
// "synced"; NULL if the config has no clock.
const char *timesync_status(void);

// lwIP's SNTP client calls this with the time, in seconds since 1970, UTC
// (SNTP_SET_SYSTEM_TIME in lwipopts.h).
void timesync_got(unsigned long unix_s);

#endif
