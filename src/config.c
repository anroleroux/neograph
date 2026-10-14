#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/flash.h"
// #include "pico/unique_id.h"
#include "pico/multicore.h"
#include "hardware/flash.h"
#include "config.h"
#include "hwman.h"
#include "keyboard.h"
#include "config/config_iterator.h"

#define PREFIX "neoGraph-"
#define PREFIX_LEN 9
#define NAME_LEN   4
#define MAC_LEN    6

void read_secrets(uint8_t* buffer, size_t len) {
    const uint8_t *flash_target_contents = (const uint8_t *)(XIP_BASE + SECURE_FLASH_TARGET_OFFSET);
    memcpy(buffer, flash_target_contents, len);
}

void read_config(uint8_t* buffer, size_t len) {
    const uint8_t *flash_target_contents = (const uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET);
    memcpy(buffer, flash_target_contents, len);
}

bool next_config_element(config_element_iterator *elms) {
    if (elms->pos >= elms->size) return false;

    if (elms->pos == 0 && elms->pay == NULL) {
        if (elms->type == PKT_CONFIG) {
            elms->pos = 3;
        }
    } else {
        elms->pos =+ (elms->pos + 5 + elms->len);
    }

    if (elms->pos >= elms->size) {
        elms->pay = NULL;
        return true;
    }
    elms->type = elms->buf[elms->pos];
    elms->id = elms->buf[elms->pos+2]<<8 | elms->buf[elms->pos+3];
    elms->len = elms->buf[elms->pos+4];
    elms->pay = elms->buf+elms->pos+5;
    // memcpy(elms->pay, elms->buf+elms->pos+5, elms->len);

    printf("debug pos:%d, size:%d, type:%X, id:%d, len:%d\n",elms->pos,elms->size,elms->type,elms->id,elms->len);
}

// Payload length of the packet stored in a flash region, from its header. 0 if the
// region holds no config or the length doesn't fit the region (corrupt flash).
static uint16_t stored_length(const uint8_t header[3], size_t region_size) {
    if (header[0] != PKT_CONFIG) return 0;
    uint16_t data_length = (header[1] << 8) | header[2];
    if (PKT_HEADER_LEN + data_length > region_size) {
        printf("stored length %u does not fit its %u-byte region, ignored\n", data_length, region_size);
        return 0;
    }
    return data_length;
}

// Erase a flash region and program a packet into it. Flash is programmed in whole
// pages, so the packet is padded with 0xFF to a page boundary. A packet bigger than
// the region is refused and nothing is erased. Core 1 is paused for the write: it
// executes from flash and would crash while flash is erased. Core 0's interrupts
// are off only for the erase and program, so core 1 can finish what it is doing
// (e.g. a printf over USB, serviced by a core 0 interrupt) before it parks.
static bool program_region(uint32_t offset, size_t region_size, const uint8_t *data, size_t length) {
    static uint8_t page_buf[FLASH_SIZE];
    if (length > region_size || length > sizeof(page_buf)) {
        printf("%u bytes do not fit the %u-byte flash region, not written\n", length, region_size);
        return false;
    }
    size_t padded = (length + FLASH_PAGE_SIZE - 1) & ~(size_t)(FLASH_PAGE_SIZE - 1);
    memcpy(page_buf, data, length);
    memset(page_buf + length, 0xFF, padded - length);

    if (!core1_pause()) {
        printf("core 1 did not pause, flash not written\n");
        return false;
    }
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(offset, region_size);
    flash_range_program(offset, page_buf, padded);
    restore_interrupts(ints);
    core1_resume();
    return true;
}

void write_secrets(const uint8_t* data, size_t length) {
    printf("storing secrets to rom\n");
    program_region(SECURE_FLASH_TARGET_OFFSET, SECURE_FLASH_SIZE, data, length);
}

void write_config(const uint8_t* data, size_t length) {
    printf("storing config to rom\n");
    program_region(FLASH_TARGET_OFFSET, FLASH_SIZE, data, length);
}

void update_secrets(const uint8_t* new_buf, size_t new_len) {
    printf("updating secrets in rom with message, len %d\n", new_len);
    for (int i = 0; i < new_len; i++) {
        printf("%02X", new_buf[i]);
        printf(" ");
    }
    printf("\n");

    uint8_t config_test1_buffer[SECURE_FLASH_SIZE];
    size_t data_test1_length = 70;
    read_secrets(config_test1_buffer,data_test1_length);
    printf("before: retrieved config (%d bytes) from rom\n", data_test1_length);

    for (int i = 0; i < data_test1_length; i++) {
        printf("%02X", config_test1_buffer[i]);
        printf(" ");
    }
    printf("\n");

    uint8_t header_buffer[3];
    read_secrets(header_buffer,3);
    uint16_t data_length = stored_length(header_buffer, SECURE_FLASH_SIZE);
    uint8_t config_buffer[SECURE_FLASH_SIZE];
    read_secrets(config_buffer,data_length+3);

    uint8_t assem_buf[SECURE_FLASH_SIZE];
    size_t assem_len = data_length
        ? config_replace_field(config_buffer, data_length+3, new_buf, new_len, assem_buf, sizeof(assem_buf))
        : 0;
    if (assem_len) {
        program_region(SECURE_FLASH_TARGET_OFFSET, SECURE_FLASH_SIZE, assem_buf, assem_len);
    } else {
        printf("no matching secret stored, or it would not fit; nothing written\n");
    }

    uint8_t config_test2_buffer[SECURE_FLASH_SIZE];
    size_t data_test2_length = 70;
    read_secrets(config_test2_buffer,data_test2_length);
    printf("after: retrieved config (%d bytes) from rom\n", data_test2_length);

    for (int i = 0; i < data_test2_length; i++) {
        printf("%02X", config_test2_buffer[i]);
        printf(" ");
    }
    printf("\n");

    printf("done updating secrets\n");
}

void update_config(const uint8_t* new_buf, size_t new_len) {
    // Static: each holds a whole config, too big for core 0's stack.
    static uint8_t config_buffer[FLASH_SIZE];
    static uint8_t assem_buf[FLASH_SIZE];

    uint8_t header_buffer[3];
    read_config(header_buffer,3);
    uint16_t data_length = stored_length(header_buffer, FLASH_SIZE);
    read_config(config_buffer,data_length+3);

    size_t assem_len = data_length
        ? config_replace_field(config_buffer, data_length+3, new_buf, new_len, assem_buf, sizeof(assem_buf))
        : 0;
    if (assem_len) {
        program_region(FLASH_TARGET_OFFSET, FLASH_SIZE, assem_buf, assem_len);
    } else {
        printf("no matching field stored, or it would not fit; nothing written\n");
    }

    printf("done updating config\n");
}

void device_name(char out[5]) {
    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);

    // Simple 32-bit hash from the device ID bytes
    uint32_t hash = 0x811C9DC5; // FNV-1a offset basis
    for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++) {
        hash ^= board_id.id[i];
        hash *= 0x01000193; // FNV-1a prime
    }

    out[0] = LETTERS[hash % 26];
    hash /= 26;

    // Convert hash to 4 base-36 characters
    for (int i = 1; i < 4; i++) {
        uint32_t index = hash % 36;
        out[i] = ALPHABET[index];
        hash /= 36;
    }

    out[4] = '\0';
}

size_t build_secrets(uint8_t *buf, const char name[5]) {
    uint8_t *p = buf;

    // --- 0xB3 block #2 ---
    *p++ = MSG_NETWORK_FIELD;
    *p++ = 0x00; // reserved
    *p++ = 0x00; // elm ID H
    *p++ = 0x00; // elm ID L
    *p++ = 0x09; // elm Len
    *p++ = FIELD_NETWORK_PASSWORD;
    memcpy(p, "47353749", 8);
    // memcpy(p, "neoGraph", 8);
    p += 8;
    // memcpy(p, name, NAME_LEN);
    // p += NAME_LEN;
    // *p++ = 0x00;

    return (size_t)(p - buf);
}

size_t build_config(uint8_t *buf, const char name[5]) {
    uint8_t *p = buf;

    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);

    // --- 0xB0 block ---
    *p++ = MSG_DEVICE;
    *p++ = 0x00; // reserved
    *p++ = 0x00; // elm ID H
    *p++ = 0x00; // elm ID L
    *p++ = PICO_UNIQUE_BOARD_ID_SIZE_BYTES+1; // elm Len
    *p++ = FIELD_DEVICE_DID;
    memcpy(p, board_id.id, PICO_UNIQUE_BOARD_ID_SIZE_BYTES);
    p += PICO_UNIQUE_BOARD_ID_SIZE_BYTES;

    // --- 0xB1 block #1 ---
    *p++ = MSG_DEVICE_FIELD;
    *p++ = 0x00; // reserved
    *p++ = 0x00; // elm ID H
    *p++ = 0x00; // elm ID L
    *p++ = 0x05; // elm Len
    *p++ = FIELD_DEVICE_NAME;
    memcpy(p, name, NAME_LEN);
    p += NAME_LEN;

    // --- 0xB1 block #2 ---
    *p++ = MSG_DEVICE_FIELD;
    *p++ = 0x00; // reserved
    *p++ = 0x00; // elm ID H
    *p++ = 0x00; // elm ID L
    *p++ = 0x03; // elm Len
    *p++ = FIELD_DEVICE_NETWORK;
    *p++ = 0x00; // ref network elm ID H
    *p++ = 0x00; // ref network elm ID L

    // --- 0xB2 block ---
    *p++ = MSG_NETWORK;
    *p++ = 0x00; // reserved
    *p++ = 0x00; // elm ID H
    *p++ = 0x00; // elm ID L
    *p++ = 0x02; // elm Len
    *p++ = FIELD_NETWORK_OPTIONS;
    *p++ = NET_OPT_JOIN;

    // --- 0xB3 block #1 ---
    *p++ = MSG_NETWORK_FIELD;
    *p++ = 0x00; // reserved
    *p++ = 0x00; // elm ID H
    *p++ = 0x00; // elm ID L
    *p++ = 0x05; // elm Len
    *p++ = FIELD_NETWORK_NAME;
    memcpy(p, name, NAME_LEN);
    p += NAME_LEN;

    // --- 0xB3 block #2 ---
    *p++ = MSG_NETWORK_FIELD;
    *p++ = 0x00; // reserved
    *p++ = 0x00; // elm ID H
    *p++ = 0x00; // elm ID L
    // *p++ = 0x0E; // elm Len
    *p++ = 0x04; // elm Len
    *p++ = FIELD_NETWORK_SSID;
    // memcpy(p, PREFIX, PREFIX_LEN);
    // p += PREFIX_LEN;
    // memcpy(p, name, NAME_LEN);
    memcpy(p, "LRX", 3);
    p += 3;

    return (size_t)(p - buf);
}

size_t wrap_secrets(uint8_t *out, const char name[5]) {
    uint8_t inner[SECURE_FLASH_SIZE];
    size_t inner_len = build_secrets(inner, name);

    uint8_t *p = out;

    *p++ = PKT_CONFIG;
    *p++ = (inner_len >> 8) & 0xFF;
    *p++ = inner_len & 0xFF;
    memcpy(p, inner, inner_len);
    p += inner_len;

    return (size_t)(p - out);
}

size_t wrap_config(uint8_t *out, const char name[5]) {
    static uint8_t inner[FLASH_SIZE];
    size_t inner_len = build_config(inner, name);

    uint8_t *p = out;

    *p++ = PKT_CONFIG;
    *p++ = (inner_len >> 8) & 0xFF;
    *p++ = inner_len & 0xFF;
    memcpy(p, inner, inner_len);
    p += inner_len;

    return (size_t)(p - out);
}



// static char log_buf[256];
// static size_t log_pos = 0;

// void log_appendf(const char *fmt, ...) {
//     va_list args;
//     va_start(args, fmt);
//     log_pos += vsnprintf(&log_buf[log_pos], sizeof(log_buf) - log_pos, fmt, args);
//     va_end(args);
// }





// ControlList* unmarshal_controls(const uint8_t* data, size_t length) {
void unmarshal_controls(const uint8_t* data, size_t length) {
    config_element_iterator itr;
    config_element_iterator_init(&itr, data, length);

    // Core 1 adds each setup to what it already has, so clear its table first:
    // the config replaces the controls rather than adding to them.
    multicore_fifo_push_blocking(fifo_op_word(FIFO_OP_CLEAN_SETUP, 0, 0, 0));
    printf("send hw clean setup\n");
    keyboard_clear();

    // size_t offset = 0;
    // size_t capacity = 4;

    while (itr.next(&itr)) {
        uint32_t control_setup_msg;

        switch (itr.type) {
        case MSG_HW_LED:
            if (itr.len != 2) break;

            control_setup_msg = fifo_op_word(FIFO_OP_SETUP, MSG_HW_LED, itr.id, 0);

            multicore_fifo_push_blocking(control_setup_msg);
            printf("send hw LED setup %X\n",control_setup_msg);
            
            control_setup_msg = fifo_ref_word(itr.pay[0]<<8 | itr.pay[1]);
            multicore_fifo_push_blocking(control_setup_msg);
            printf("send hw LED setup %X\n",control_setup_msg);
            break;
        
        case MSG_HW_GPO:
            if (itr.len != 3) break;

            control_setup_msg = fifo_op_word(FIFO_OP_SETUP, MSG_HW_GPO, itr.id, itr.pay[2]);

            multicore_fifo_push_blocking(control_setup_msg);
            printf("send hw GPO setup %X\n",control_setup_msg);

            control_setup_msg = fifo_ref_word(itr.pay[0]<<8 | itr.pay[1]);

            multicore_fifo_push_blocking(control_setup_msg);
            printf("send hw GPO setup %X\n",control_setup_msg);
            break;

        case MSG_HW_PWM:
            if (itr.len != 15) break;

            control_setup_msg = fifo_op_word(FIFO_OP_SETUP, MSG_HW_PWM, itr.id, itr.pay[2]);
            multicore_fifo_push_blocking(control_setup_msg);
            printf("send hw PWM setup %X\n",control_setup_msg);

            control_setup_msg = fifo_ref_word(itr.pay[0]<<8 | itr.pay[1]);
            multicore_fifo_push_blocking(control_setup_msg);
            printf("send hw PWM setup %X\n",control_setup_msg);

            control_setup_msg = fifo_pwm_word(itr.pay[3]<<8 | itr.pay[4], itr.pay[5]);
            multicore_fifo_push_blocking(control_setup_msg);
            printf("send hw PWM setup %X\n",control_setup_msg);

            control_setup_msg = fifo_duty_word(itr.pay[6], itr.pay[7], itr.pay[8]);
            multicore_fifo_push_blocking(control_setup_msg);
            printf("send hw PWM setup %X\n",control_setup_msg);

            control_setup_msg = fifo_ref_word(itr.pay[9]<<8 | itr.pay[10]);
            multicore_fifo_push_blocking(control_setup_msg);
            printf("send hw PWM setup %X\n",control_setup_msg);

            control_setup_msg = fifo_freq_word(itr.pay[11]<<8 | itr.pay[12], itr.pay[13]<<8 | itr.pay[14]);
            multicore_fifo_push_blocking(control_setup_msg);
            printf("send hw PWM setup %X\n",control_setup_msg);
            break;

        case MSG_HW_KEYBOARD:
            // Core 0 keeps the keystroke and types it; core 1 says when.
            if (!keyboard_payload_valid(itr.pay, itr.len)) break;
            if (!keyboard_add(itr.id, itr.pay + 2, itr.len - 2)) {
                printf("skipping keyboard %d: too many keyboards\n", itr.id);
                break;
            }

            control_setup_msg = fifo_op_word(FIFO_OP_SETUP, MSG_HW_KEYBOARD, itr.id, 0);
            multicore_fifo_push_blocking(control_setup_msg);
            printf("send hw keyboard setup %X\n",control_setup_msg);

            control_setup_msg = fifo_ref_word(itr.pay[0]<<8 | itr.pay[1]);
            multicore_fifo_push_blocking(control_setup_msg);
            printf("send hw keyboard setup %X\n",control_setup_msg);
            break;

        case MSG_HW_ADC:
            if (itr.len != 1 || itr.pay[0] >= ADC_CHANNELS) break;

            control_setup_msg = fifo_op_word(FIFO_OP_SETUP, MSG_HW_ADC, itr.id, itr.pay[0]);
            multicore_fifo_push_blocking(control_setup_msg);
            printf("send hw ADC setup %X\n",control_setup_msg);
            break;

        default:
            printf("skipping control type %02X\n",itr.type);
            break;
        }
    }

    // while (offset + 4 <= length) {
    //     Control control = {0};
    //     control.type = data[offset++];
    //     offset++;
    //     control.id = (data[offset] << 8) | data[offset + 1];
    //     offset += 2;

    //     if (control.type < 0x20 || control.type > 0x3F) {
    //         printf("Unsupported control type: 0x%02X, skipping %i\n", control.type,data[offset]);
    //         offset += data[offset++];
    //         continue;
    //     }
            
    // // printf("control type %02X, offset %i, length %i\n",control.type,offset,length);
    //     uint32_t control_setup_msg;
    //     switch (control.type) {
    //         case CONTROL_TYPE_LED:
    //             if (offset + 1 > length) break;  // bounds check
    //             // control.payload_type = PAYLOAD_ID_REF;
    //             // control.ref_id = (data[offset] << 8) | data[offset + 1];
    //             offset += 1;

    //             control_setup_msg = (CONFIG_OPERATION_TYPE_SETUP << 24)
    //                 | (CONTROL_TYPE_LED << 16)
    //                 | control.id;
    //             // printf("send %X\n",control_setup_msg);
    //             multicore_fifo_push_blocking(control_setup_msg);
    //             break;
        
    //         case CONTROL_TYPE_GPIO:
    //             if (offset + 1 > length) break;  // bounds check
    //             offset += 1;

    //             control_setup_msg = (CONFIG_OPERATION_TYPE_SETUP << 24)
    //                 | (CONTROL_TYPE_GPIO << 16)
    //                 | (data[offset++] << 27)
    //                 | control.id;
    //             // printf("send %X\n",control_setup_msg);
    //             multicore_fifo_push_blocking(control_setup_msg);
    //             break;
        
    //         case CONTROL_TYPE_PWM:
    //             if (offset + 1 > length) break;  // bounds check
    //             offset += 1;

    //             control_setup_msg = (CONFIG_OPERATION_TYPE_SETUP << 24)
    //                 | (CONTROL_TYPE_PWM << 16)
    //                 | (data[offset++] << 27)
    //                 | control.id;
    //             // printf("send %X\n",control_setup_msg);
    //             multicore_fifo_push_blocking(control_setup_msg);

    //             control_setup_msg = (data[offset++] << 24)
    //                 | (data[offset++] << 16)
    //                 | (data[offset++] << 8)
    //                 | data[offset++];
    //             // printf("send %X\n",control_setup_msg);
    //             multicore_fifo_push_blocking(control_setup_msg);

    //             control_setup_msg = (data[offset++] << 24)
    //                 | (data[offset++] << 16)
    //                 | (data[offset++] << 8)
    //                 | data[offset++];
    //             // printf("send %X\n",control_setup_msg);
    //             multicore_fifo_push_blocking(control_setup_msg);

    //             control_setup_msg = (data[offset++] << 24)
    //                 | (data[offset++] << 16)
    //                 | (data[offset++] << 8)
    //                 | data[offset++];
    //             // printf("send %X\n",control_setup_msg);
    //             multicore_fifo_push_blocking(control_setup_msg);

    //             break;

    //         // case 0xA0:
    //         //     if (offset + 1 > length) break;  // bounds check
    //         //     // control.payload_type = PAYLOAD_STRING;
    //         //     control.string_payload.length = data[offset++];
    //         //     if (offset + control.string_payload.length > length) break;
        
    //         //     control.string_payload.str = malloc(control.string_payload.length + 1);
    //         //     memcpy(control.string_payload.str, data + offset, control.string_payload.length);
    //         //     control.string_payload.str[control.string_payload.length] = '\0';  // null-terminate
    //         //     offset += control.string_payload.length;
    //         //     break;
        
    //         default:
    //             printf("skipping control type %02X\n",control.payload_type);
    //             // control.payload_type = PAYLOAD_NONE;
    //             break;
    //     }

    //     // if (list->count >= capacity) {
    //     //     capacity *= 2;
    //     //     Control* new_controls = realloc(list->controls, capacity * sizeof(Control));
    //     //     if (!new_controls) {
    //     //         fprintf(stderr, "Memory reallocation failed\n");
    //     //         free(list->controls);
    //     //         exit(1);
    //     //     }
    //     //     list->controls = new_controls;
    //     // }

    //     // list->controls[list->count++] = control;
    // }

    // return list;
}

int unmarshal_network_config(
    uint8_t *data,
    size_t length, 
    const uint8_t *target_did,
    network_setup *out
) {
    config_element_iterator itr;
    config_element_iterator_init(&itr, data, length);

    // config_element_iterator itr = {
    //     .buf = buf,
    //     .pay = NULL,
    //     .pos = 0,
    //     .size = size
    // };

    while (itr.next(&itr)) {
        if (itr.type == MSG_DEVICE) {
            // printf("got B0\n");
            if (memcmp(itr.pay+1, target_did, PICO_UNIQUE_BOARD_ID_SIZE_BYTES) == 0) {
                // printf("match DID\n");
                out->device.id = itr.id;
                memcpy(out->device.did,itr.pay+1,PICO_UNIQUE_BOARD_ID_SIZE_BYTES);
                break;
            }
        }
    }

    itr.reset(&itr);

    while (itr.next(&itr)) {
        if (itr.type == MSG_DEVICE_FIELD) {
            // printf("got B1\n");
            if (itr.id != out->device.id) continue;
            uint8_t field_number = itr.pay[0];
            if (field_number == FIELD_DEVICE_NAME) {
                // printf("match device name\n");
                memcpy(out->device.name, itr.pay+1, itr.len-1);
                // printf("debug name:%s\n", out->device.name);
                continue;
            }

            if (field_number == FIELD_DEVICE_NETWORK) {
                // printf("match device network\n");
                out->network.id = itr.pay[1]<<8 | itr.pay[2];
                continue;
            }
        }
    }

    itr.reset(&itr);

    while (itr.next(&itr)) {
        if (itr.type == MSG_NETWORK) {
            // printf("got B2\n");
            if (itr.id != out->network.id) continue;
            uint8_t field_number = itr.pay[0];

            if (field_number == FIELD_NETWORK_OPTIONS) {
                // printf("match network opts\n");
                out->network.opts = itr.pay[1];
                // printf("debug opts:%d\n", out->network.opts);
                continue;
            }
        }
    }

    itr.reset(&itr);

    while (itr.next(&itr)) {
        if (itr.type == MSG_NETWORK_FIELD) {
            // printf("got B3\n");
            if (itr.id != out->network.id) continue;
            uint8_t field_number = itr.pay[0];

            if (field_number == FIELD_NETWORK_NAME) {
                // printf("match network name\n");
                memcpy(out->network.name, itr.pay+1, itr.len-1);
                // printf("debug name:%s\n", out->network.name);
                continue;
            }

            if (field_number == FIELD_NETWORK_SSID) {
                // printf("match network ssid\n");
                memcpy(out->network.ssid, itr.pay+1, itr.len-1);
                // printf("debug ssid:%s\n", out->network.ssid);
                continue;
            }

            if (field_number == FIELD_NETWORK_PASSWORD) {
                // printf("match network password\n");
                memcpy(out->network.pass, itr.pay+1, itr.len-1);
                // printf("debug pass:%s\n", out->network.pass);
                continue;
            }
        }
    }

    return 1;
}

void retrieve_networking_config(network_setup *out) {
    printf("reading networking config from rom\n");
    // uint32_t ints = save_and_disable_interrupts();

    uint8_t header_buffer[3];
    read_config(header_buffer,3);
    if (header_buffer[0] != PKT_CONFIG) {
        printf("no networking config found, creating default config\n");

        char name[NAME_LEN+1];
        device_name(name);

        static uint8_t config[FLASH_SIZE];
        size_t config_length = wrap_config(config, name);
        // printf("debug config_length %d\n",config_length);
        write_config(config, config_length);

        uint8_t secrets[SECURE_FLASH_SIZE];
        size_t secrets_length = wrap_secrets(secrets, name);
        // printf("debug secrets_length %d\n",secrets_length);
        write_secrets(secrets, secrets_length);

        // for (int i = 0; i < secrets_length; i++) {
        //     printf("%02X", secrets[i]);
        //     printf(" ");
        // }
        // printf("\n");
    }
    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);

    // printf("\nDevice ID: ");
    // for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++) {
    //     printf("%02X", board_id.id[i]);
    //     if (i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES-1) printf(" ");
    // }

    read_config(header_buffer,3);
    uint16_t data_length = stored_length(header_buffer, FLASH_SIZE);
    // printf("debug: header %02X %02X %02X length %d \n",header_buffer[0],header_buffer[1],header_buffer[2],data_length);

    static uint8_t config_buffer[FLASH_SIZE];
    read_config(config_buffer,data_length+3);
    // printf("retrieved config (%d bytes) from rom\n", data_length);

    // for (int i = 0; i < data_length+3; i++) {
    //     printf("%02X", config_buffer[i]);
    //     printf(" ");
    // }
    // printf("\n");

    unmarshal_network_config(config_buffer+3,data_length,board_id.id,out);

    read_secrets(header_buffer,3);
    data_length = stored_length(header_buffer, SECURE_FLASH_SIZE);
    // printf("debug: header %02X %02X %02X length %d \n",header_buffer[0],header_buffer[1],header_buffer[2],data_length);

    uint8_t secrets_buffer[SECURE_FLASH_SIZE];
    read_secrets(secrets_buffer,data_length+3);
    // printf("retrieved secrets (%d bytes) from rom\n", data_length);

    unmarshal_network_config(secrets_buffer+3,data_length,board_id.id,out);
}

uint16_t retrieve_store(uint8_t* data_buffer) {
    printf("retrieving config from rom\n");
    uint32_t ints = save_and_disable_interrupts();

    uint8_t header_buffer[3];
    read_config(header_buffer,3);
    uint16_t data_length = stored_length(header_buffer, FLASH_SIZE);
    if (data_length == 0) {
        restore_interrupts(ints);
        printf("retrieved %d %d %d, no config stored\n", header_buffer[0], header_buffer[1], header_buffer[2]);
        return 0;
    }

    read_config(data_buffer,data_length+3);
    restore_interrupts(ints);
    printf("retrieved %d bytes from rom\n", data_length);
    unmarshal_controls(data_buffer+3,data_length);

    return data_length;
}

void apply_config(const uint8_t* data, size_t length) {
    if (length > PKT_CONFIG_MAX) {
        printf("config of %u bytes is over the %u-byte limit, not applied\n", length, PKT_CONFIG_MAX);
        return;
    }
    write_config(data,length);
    printf("unmarshalling config\n");
    unmarshal_controls(data+3,length-3);
}