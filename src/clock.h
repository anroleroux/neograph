#ifndef CLOCK_H_INCLUDED
#define CLOCK_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"

/*
 * The device's clock (MSG_HW_CLOCK), a time of day: the arithmetic, with no hardware
 * here, so the tests run it too. hwman.c keeps it from the microsecond timer.
 * PROTOCOL.md, "Clocks", describes it.
 */

#define CLOCK_DAY_MS    86400000u

// A time of day in milliseconds from midnight: its hour, minute and second.
void clock_parts(uint32_t ms, uint8_t parts[3]);

// The offset to add to the milliseconds the timer has counted (`now_ms`) so the time
// of day is h:m:s, from its start, now. False, leaving *offset, if it is not a time
// of day: an hour over 23, a minute or second over 59.
bool clock_offset_for(uint64_t now_ms, uint8_t h, uint8_t m, uint8_t s, uint32_t *offset);

// The time of day, in milliseconds from midnight, when the timer has counted `now_ms`.
uint32_t clock_time_of_day(uint64_t now_ms, uint32_t offset);

// The offset to set the clock to `ms` from midnight now (as clock_offset_for).
uint32_t clock_offset_to(uint64_t now_ms, uint32_t ms);

// An offset moved by a number of minutes, forwards or back: a time zone changed.
uint32_t clock_shift(uint32_t offset, int32_t minutes);

// The time of day, in milliseconds from midnight, in a time zone `zone` minutes ahead
// of UTC, at Unix time `unix_s` (seconds since 1970, UTC), as a time server gives it.
uint32_t clock_from_unix(uint32_t unix_s, int32_t zone);

#endif
