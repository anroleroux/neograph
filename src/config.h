#ifndef CONFIG_H_INCLUDED
#define CONFIG_H_INCLUDED

#include "pico/unique_id.h"
#include "protocol.h"

#define FLASH_SIZE 0x1000
#define SECURE_FLASH_SIZE 0x100

#define FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SIZE)
#define SECURE_FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SIZE - SECURE_FLASH_SIZE)

static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static const char LETTERS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

typedef struct {
    uint16_t id;
    uint8_t did[PICO_UNIQUE_BOARD_ID_SIZE_BYTES];
    char name[32];
    uint16_t network;
} device_config;

typedef struct {
    uint16_t id;
    uint8_t opts;
    char name[32];
    char ssid[32];
    char pass[32];
} network_config;

typedef struct {
    device_config device;
    network_config network;
} network_setup;

void retrieve_networking_config(network_setup *out);
void apply_config(const uint8_t* data, size_t length);
void update_config(const uint8_t* new_val, size_t new_len);
void update_secrets(const uint8_t* new_val, size_t new_len);
uint16_t retrieve_store(uint8_t* data_buffer);

#endif