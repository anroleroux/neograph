#include "clock.h"

void clock_parts(uint32_t ms, uint8_t parts[3]) {
    uint32_t s = ms % CLOCK_DAY_MS / 1000;
    parts[0] = (uint8_t)(s / 3600);
    parts[1] = (uint8_t)(s / 60 % 60);
    parts[2] = (uint8_t)(s % 60);
}

uint32_t clock_offset_to(uint64_t now_ms, uint32_t ms) {
    uint32_t at = (uint32_t)(now_ms % CLOCK_DAY_MS);
    return (ms % CLOCK_DAY_MS + CLOCK_DAY_MS - at) % CLOCK_DAY_MS;
}

bool clock_offset_for(uint64_t now_ms, uint8_t h, uint8_t m, uint8_t s, uint32_t *offset) {
    if (h >= CLOCK_HOURS || m >= CLOCK_MINUTES || s >= CLOCK_SECONDS) return false;
    *offset = clock_offset_to(now_ms, ((uint32_t)h * 3600 + (uint32_t)m * 60 + s) * 1000);
    return true;
}

uint32_t clock_shift(uint32_t offset, int32_t minutes) {
    int64_t moved = ((int64_t)offset + (int64_t)minutes * 60000) % CLOCK_DAY_MS;
    return (uint32_t)(moved < 0 ? moved + CLOCK_DAY_MS : moved);
}

uint32_t clock_from_unix(uint32_t unix_s, int32_t zone) {
    return clock_shift((uint32_t)(unix_s % 86400u) * 1000u, zone);
}

uint32_t clock_time_of_day(uint64_t now_ms, uint32_t offset) {
    return (uint32_t)((now_ms % CLOCK_DAY_MS + offset) % CLOCK_DAY_MS);
}
