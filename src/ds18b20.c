#include <string.h>

#include "ds18b20.h"

uint8_t ds18b20_crc8(const uint8_t *data, uint8_t n) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < n; i++) {
        uint8_t b = data[i];
        for (int k = 0; k < 8; k++) {
            uint8_t mix = (crc ^ b) & 1;
            crc >>= 1;
            if (mix) crc ^= 0x8C;       // the polynomial, bit-reversed
            b >>= 1;
        }
    }
    return crc;
}

uint16_t ds18b20_conversion_ms(uint8_t resolution) {
    static const uint16_t ms[] = { 94, 188, 375, 750 };
    return ms[resolution & DS18B20_RES_MASK];
}

uint8_t ds18b20_config(uint8_t resolution) {
    return (uint8_t)((resolution & DS18B20_RES_MASK) << 5 | 0x1F);
}

bool ds18b20_celsius(const uint8_t pad[DS18B20_PAD_LEN], uint8_t resolution, float *celsius) {
    static const uint8_t zeros[DS18B20_PAD_LEN] = { 0 };
    static const uint8_t ones[DS18B20_PAD_LEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    if (!memcmp(pad, zeros, DS18B20_PAD_LEN) || !memcmp(pad, ones, DS18B20_PAD_LEN)) return false;
    if (ds18b20_crc8(pad, DS18B20_PAD_LEN) != 0) return false;
    // 1/16 °C a count, two's complement; at 9 bits the low 3 bits are undefined, at 10 the
    // low 2, at 11 the low 1.
    uint16_t raw = (uint16_t)(pad[1] << 8 | pad[0]);
    raw &= (uint16_t)~((1u << (3 - (resolution & DS18B20_RES_MASK))) - 1);
    *celsius = (int16_t)raw / 16.0f;
    return true;
}

bool ds18b20_take(float celsius, bool found, float last, bool *doubted) {
    bool was = *doubted;
    *doubted = false;
    if (celsius != DS18B20_POWER_ON_C || (found && last > DS18B20_POWER_ON_C - 10) || was) return true;
    *doubted = true;
    return false;
}
