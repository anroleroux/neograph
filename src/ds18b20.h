#ifndef DS18B20_H_INCLUDED
#define DS18B20_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"

/*
 * A DS18B20 temperature sensor (MSG_HW_DS18B20), as core 1 reads it: the 1-Wire
 * commands, the checks and the arithmetic, with no hardware here, so the tests run it
 * too. hwman.c drives the pin. PROTOCOL.md, "DS18B20 temperature sensors", describes it.
 */

#define DS18B20_SKIP_ROM        0xCC    /* the one sensor on the line: no address */
#define DS18B20_CONVERT         0x44    /* measure the temperature */
#define DS18B20_WRITE_PAD       0x4E    /* write TH, TL and the configuration register */
#define DS18B20_READ_PAD        0xBE    /* read the 9-byte scratchpad */
#define DS18B20_PAD_LEN         9       /* temperature LSB, MSB, TH, TL, config, 3 reserved, CRC */
#define DS18B20_POWER_ON_C      85.0f   /* what it holds before its first conversion */
#define DS18B20_FAILS_LOST      3       /* reads in a row that fail before it counts as gone */

// The Dallas/Maxim CRC-8 (x^8 + x^5 + x^4 + 1) of n bytes: 0 over a whole scratchpad,
// CRC included, when it came in whole.
uint8_t ds18b20_crc8(const uint8_t *data, uint8_t n);

// Milliseconds a conversion takes at a resolution (DS18B20_RES_): 94 to 750.
uint16_t ds18b20_conversion_ms(uint8_t resolution);

// Its configuration register for a resolution: the resolution in bits 5-6, the rest 1s.
uint8_t ds18b20_config(uint8_t resolution);

// The temperature in a scratchpad, in °C, at a resolution: its low bits past the
// resolution are undefined, so dropped. Returns false, leaving *celsius, if the
// scratchpad is not one: its CRC is wrong, or it is all 0s or all 1s (no sensor
// answering: the line held low, or left high).
bool ds18b20_celsius(const uint8_t pad[DS18B20_PAD_LEN], uint8_t resolution, float *celsius);

// Whether to take a reading, given the last one taken (`found`: whether there is
// one). 85 °C is what the sensor holds from power-on until it converts, so one read
// just after it lost power for a moment: an 85 °C reading is dropped once, unless the
// last reading was within 10 °C of it; the next in a row is taken. *doubted carries
// whether the last reading was dropped so.
bool ds18b20_take(float celsius, bool found, float last, bool *doubted);

#endif
