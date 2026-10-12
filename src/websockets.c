#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/unique_id.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/sockets.h"
#include "mbedtls/sha1.h"
#include "pico/multicore.h"

#include "websockets.h"
#include "tcpserver.h"
#include "config.h"
#include "config/config_iterator.h"

void compute_websocket_accept(const char *client_key, char *accept_key) {
    const char *websocket_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char key_concat[128];
    unsigned char sha1_hash[20];
    
    snprintf(key_concat, sizeof(key_concat), "%s%s", client_key, websocket_guid);
    
    // Compute SHA-1 hash
    mbedtls_sha1((unsigned char *)key_concat, strlen(key_concat), sha1_hash);

    // Base64 encode the SHA-1 hash
    base64_encode(sha1_hash, 20, accept_key, 64);
}

const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void base64_encode(const unsigned char *input, int input_length, char *output, int output_size) {
    int i = 0, j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    if (output_size < ((input_length + 2) / 3) * 4 + 1) {
        return; // Output buffer too small
    }

    while (input_length--) {
        char_array_3[i++] = *(input++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; i < 4; i++)
                output[j++] = base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for (int k = i; k < 3; k++)
            char_array_3[k] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (int k = 0; k < i + 1; k++)
            output[j++] = base64_chars[char_array_4[k]];

        while ((i++ < 3))
            output[j++] = '=';
    }

    output[j] = '\0';
}
// Function to unmask WebSocket payload
void unmask_payload(uint8_t *payload, size_t length, uint8_t mask[4]) {
    for (size_t i = 0; i < length; i++) {
        payload[i] ^= mask[i % 4];
    }
}

void send_websocket_message(void *arg, const char *message) {
    tcpclient *clientstate = (tcpclient*)arg;

    size_t message_length = strlen(message);
    uint8_t frame[1024];

    frame[0] = 0x81;  // FIN = 1, OpCode = 0x1 (Text)
    
    if (message_length <= 125) {
        frame[1] = message_length;
        memcpy(frame + 2, message, message_length);
        tcp_write(clientstate->client_pcb, frame, message_length + 2, TCP_WRITE_FLAG_COPY);
        tcp_output(clientstate->client_pcb);
    } else if (message_length <= 65535) {
        frame[1] = 126;
        frame[2] = (message_length >> 8) & 0xFF;
        frame[3] = message_length & 0xFF;
        memcpy(frame + 4, message, message_length);
        tcp_write(clientstate->client_pcb, frame, message_length + 4, TCP_WRITE_FLAG_COPY);
        tcp_output(clientstate->client_pcb);
    }
}

void send_websocket_binary(void *arg, const uint8_t *data, size_t data_length) {
    tcpclient *clientstate = (tcpclient*)arg;

    uint8_t frame[1024]; // Ensure this is large enough for your largest payload

    frame[0] = 0x82;  // FIN = 1, OpCode = 0x2 (Binary)

    if (data_length <= 125) {
        frame[1] = data_length;
        memcpy(frame + 2, data, data_length);
        tcp_write(clientstate->client_pcb, frame, data_length + 2, TCP_WRITE_FLAG_COPY);
    } else if (data_length <= 65535) {
        frame[1] = 126;
        frame[2] = (data_length >> 8) & 0xFF;
        frame[3] = data_length & 0xFF;
        memcpy(frame + 4, data, data_length);
        tcp_write(clientstate->client_pcb, frame, data_length + 4, TCP_WRITE_FLAG_COPY);
    // } else {
    //     // For payloads larger than 65535, use 64-bit length encoding (not common)
    //     frame[1] = 127;
    //     // Fill 8 bytes for 64-bit length (only last 4 bytes used here)
    //     frame[2] = frame[3] = frame[4] = frame[5] = 0;
    //     frame[6] = (data_length >> 24) & 0xFF;
    //     frame[7] = (data_length >> 16) & 0xFF;
    //     frame[8] = (data_length >> 8) & 0xFF;
    //     frame[9] = data_length & 0xFF;
    //     memcpy(frame + 10, data, data_length);
    //     tcp_write(clientstate->client_pcb, frame, data_length + 10, TCP_WRITE_FLAG_COPY);
    }

    tcp_output(clientstate->client_pcb);
}

void print_buf(const uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        printf("%02x", buf[i]);
        if (i % 16 == 15)
            printf("\n");
        else
            printf(" ");
    }
}

err_t handle_websocket_req(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    tcpclient *clientstate = (tcpclient*)arg;

    printf("tcp_server_recv %d bytes, err %d\n", p->tot_len, err);
    char buffer[1024];
    pbuf_copy_partial(p, buffer, sizeof(buffer) - 1, 0);
    buffer[p->tot_len] = '\0';  // Ensure null termination
    printf("%s\n",buffer);

    tcp_recved(tpcb, p->tot_len);  // Notify lwIP that data is received

    // Find "Sec-WebSocket-Key" in the request
    char *key_start = strstr(buffer, "Sec-WebSocket-Key: ");
    if (!key_start) {
        return tcp_client_close(clientstate);
    }

    key_start += 19; // Move past "Sec-WebSocket-Key: "
    char *key_end = strstr(key_start, "\r\n");
    if (!key_end) {
        return tcp_client_close(clientstate);
    }

    char client_key[32] = {0};
    strncpy(client_key, key_start, key_end - key_start);

    // printf("client-key:%s\n",client_key);
    char accept_key[64] = {0};
    compute_websocket_accept(client_key, accept_key);
    // printf("accept-key:%s\n",accept_key);

    // Send WebSocket handshake response
    char response[256];
    snprintf(response, sizeof(response),
             "HTTP/1.1 101 Switching Protocols\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Accept: %s\r\n\r\n",
             accept_key);

    // send(client_sock, response, strlen(response), 0);

    printf("WebSocket connected\n");
    clientstate->upgraded = true;

    // Send back the received message
    tcp_write(tpcb, response, strlen(response), TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);

    pbuf_free(p);

    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);

    uint8_t did_packet[3+PICO_UNIQUE_BOARD_ID_SIZE_BYTES];
    did_packet[0] = PKT_DEVICE_ID; did_packet[1] = 0x00; did_packet[2] = PICO_UNIQUE_BOARD_ID_SIZE_BYTES;
    memcpy(did_packet+3, board_id.id, PICO_UNIQUE_BOARD_ID_SIZE_BYTES);
    send_websocket_binary(clientstate, did_packet, 3+PICO_UNIQUE_BOARD_ID_SIZE_BYTES);

    uint8_t config[FLASH_SIZE];
    // uint16_t config_length = 23;
    uint16_t config_length = retrieve_store(config);
    // uint8_t config_msg[config_length+7];
    // snprintf(config_msg,config_length+7,"config:")
    if (config[0] == PKT_CONFIG) {
        printf("config exist, sending to client\n");
        send_websocket_binary(clientstate, config, config_length+3);
        // print_buf(config,config_length+3);
    }

    return ERR_OK;
}

err_t handle_websocket_msg(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    tcpclient *clientstate = (tcpclient*)arg;

    // printf("tcp_server_recv %d bytes, err %d\n", p->tot_len, err);
    uint8_t buffer[1024];
    pbuf_copy_partial(p, buffer, sizeof(buffer) - 1, 0);
    buffer[p->tot_len] = '\0';
    tcp_recved(tpcb, p->tot_len);

    // Extract WebSocket frame header
    uint8_t fin = buffer[0] >> 7;
    uint8_t opcode = buffer[0] & 0x0F;
    uint8_t masked = buffer[1] >> 7;
    uint64_t payload_length = buffer[1] & 0x7F;

    int header_size = 2;
    if (payload_length == 126) {
        payload_length = (buffer[2] << 8) | buffer[3];
        header_size += 2;
    } else if (payload_length == 127) {
        payload_length = 0;
        for (int i = 0; i < 8; i++) {
            payload_length |= (uint64_t)buffer[2 + i] << (8 * (7 - i));
        }
        header_size += 8;
    }

    if (masked) {
        uint8_t mask[4];
        memcpy(mask, buffer + header_size, 4);
        header_size += 4;
        unmask_payload(buffer + header_size, payload_length, mask);
    }
    buffer[header_size + payload_length] = '\0';

    if (opcode == 0x1) {  // Text frame
        printf("Text Message: %s\n", buffer + header_size);
    } else if (opcode == 0x2) {  // Binary frame
        uint8_t *payload = buffer + header_size;
        uint16_t data_length = (payload[1] << 8) | payload[2];
        // printf("length: %i, Data: %X\n", data_length,*payload);

        if (*payload == PKT_CONFIG) {
            apply_config(payload,data_length+3);
        } else if (*payload == PKT_UPDATE_CONFIG) {
            update_config(payload+3,data_length);
        } else if (*payload == PKT_UPDATE_SECRET) {
            update_secrets(payload+3,data_length);
        } else if (*payload == PKT_COMMAND) {
            config_element_iterator itr;
            config_element_iterator_init(&itr, payload+3, data_length);
            
            while (itr.next(&itr)) {
                // printf("value %i [%X %X %X %X]\n", iValue, *(payload+(7+iValue*4)), *(payload+(8+iValue*4)), *(payload+(9+iValue*4)), *(payload+(10+iValue*4)));
                uint32_t hwop_buf = fifo_op_word(FIFO_OP_CONTROL_ACTION, itr.type, itr.id, itr.pay[0]);
                multicore_fifo_push_blocking(hwop_buf);

                for (size_t i = 1; i < itr.len; i+=4) {
                    uint32_t value = itr.pay[i] << 24
                        | itr.pay[i+1] << 16
                        | itr.pay[i+2] << 8
                        | itr.pay[i+3];
                    multicore_fifo_push_blocking(value);
                }
            }
        }
    } else if (opcode == 0x8) {  // Close frame
        printf("Client requested to close WebSocket\n");
        // break;
        return tcp_client_close(clientstate);
    } else if (opcode == 0x9) {  // Ping frame
        printf("Ping received, sending Pong...\n");
        uint8_t pong_frame[2] = {0x8A, 0x00};  // Pong frame (0xA opcode)
        // send(client_sock, pong_frame, sizeof(pong_frame), 0);
        tcp_write(tpcb, pong_frame, strlen(pong_frame), TCP_WRITE_FLAG_COPY);
        tcp_output(tpcb);
    }
    
    pbuf_free(p);
    return ERR_OK;
}