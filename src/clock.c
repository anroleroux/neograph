#include "clock.h"

void clock_parts(uint32_t ms, uint8_t parts[3]) {
    uint32_t s = ms % CLOCK_DAY_MS / 1000;
    parts[0] = (uint8_t)(s / 3600);
    parts[1] = (uint8_t)(s / 60 % 60);
    parts[2] = (uint8_t)(s % 60);
}

bool clock_offset_for(uint64_t now_ms, uint8_t h, uint8_t m, uint8_t s, uint32_t *offset) {
    if (h >= CLOCK_HOURS || m >= CLOCK_MINUTES || s >= CLOCK_SECONDS) return false;
    uint32_t want = ((uint32_t)h * 3600 + (uint32_t)m * 60 + s) * 1000;
    uint32_t at = (uint32_t)(now_ms % CLOCK_DAY_MS);
    *offset = (want + CLOCK_DAY_MS - at) % CLOCK_DAY_MS;
    return true;
}

uint32_t clock_time_of_day(uint64_t now_ms, uint32_t offset) {
    return (uint32_t)((now_ms % CLOCK_DAY_MS + offset) % CLOCK_DAY_MS);
}
