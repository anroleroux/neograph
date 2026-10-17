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
#include "hwman.h"
#include "config.h"
#include "config/config_iterator.h"
#include "networking/ws_frame.h"

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

// Send one WebSocket frame. The header and the data are queued separately, so a
// frame is limited by lwIP's send buffer (TCP_SND_BUF), not by a local copy.
static err_t send_websocket_frame(tcpclient *clientstate, uint8_t opcode, const uint8_t *data, size_t length) {
    struct tcp_pcb *pcb = clientstate->client_pcb;

    uint8_t header[4];
    size_t header_length = ws_frame_header(header, opcode, length);
    if (header_length == 0) {
        printf("websocket frame of %u bytes is too long\n", length);
        return ERR_VAL;
    }
    if (tcp_sndbuf(pcb) < header_length + length) {
        printf("websocket frame of %u bytes does not fit the send buffer (%u free)\n", length, tcp_sndbuf(pcb));
        return ERR_MEM;
    }

    err_t err = tcp_write(pcb, header, header_length, TCP_WRITE_FLAG_COPY | (length ? TCP_WRITE_FLAG_MORE : 0));
    if (err == ERR_OK && length) err = tcp_write(pcb, data, length, TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK) err = tcp_output(pcb);
    if (err != ERR_OK) printf("websocket send failed: %d\n", err);
    return err;
}

void send_websocket_message(void *arg, const char *message) {
    send_websocket_frame((tcpclient*)arg, WS_OP_TEXT, (const uint8_t *)message, strlen(message));
}

void send_websocket_binary(void *arg, const uint8_t *data, size_t data_length) {
    send_websocket_frame((tcpclient*)arg, WS_OP_BINARY, data, data_length);
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
    static char buffer[1024];
    uint16_t copied = pbuf_copy_partial(p, buffer, sizeof(buffer) - 1, 0);
    buffer[copied] = '\0';  // Ensure null termination
    printf("%s\n",buffer);

    tcp_recved(tpcb, p->tot_len);  // Notify lwIP that data is received

    // Find "Sec-WebSocket-Key" in the request
    char *key_start = strstr(buffer, "Sec-WebSocket-Key: ");
    if (!key_start) {
        pbuf_free(p);
        return tcp_client_close(clientstate);
    }

    key_start += 19; // Move past "Sec-WebSocket-Key: "
    char *key_end = strstr(key_start, "\r\n");
    if (!key_end) {
        pbuf_free(p);
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

    // Frames are reassembled here; one can span several TCP segments.
    clientstate->rx = malloc(WS_RX_BUF);
    clientstate->rx_len = 0;
    if (!clientstate->rx) {
        printf("no memory for the websocket receive buffer\n");
        pbuf_free(p);
        return tcp_client_close(clientstate);
    }

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

    // Static: a config is up to PKT_CONFIG_MAX bytes, too big for core 0's stack.
    static uint8_t config[FLASH_SIZE];
    uint16_t config_length = retrieve_store(config);
    if (config_length > 0) {
        // Only sent: core 1 has run it since boot, or since it was saved.
        printf("config exist, sending to client\n");
        send_websocket_binary(clientstate, config, config_length+3);
    }

    return ERR_OK;
}

// Handle one complete binary frame: a packet (see PROTOCOL.md).
static void handle_packet(uint8_t *payload, size_t length) {
    int data_length = packet_data_length(payload, length);
    if (data_length < 0) {
        if (length < PKT_HEADER_LEN) printf("packet too short (%u bytes), ignored\n", length);
        else printf("packet says %u bytes but the frame holds %u, ignored\n",
                    (payload[1] << 8) | payload[2], length - PKT_HEADER_LEN);
        return;
    }

    if (*payload == PKT_CONFIG) {
        apply_config(payload,data_length+3);
    } else if (*payload == PKT_UPDATE_CONFIG) {
        update_config(payload+3,data_length);
    } else if (*payload == PKT_UPDATE_SECRET) {
        update_secrets(payload+3,data_length);
    } else if (*payload == PKT_COMMAND) {
        config_element_iterator itr;
        config_element_iterator_init(&itr, payload+3, data_length);

        // One action word per payload byte: byte i is the new value of the element's output i.
        while (itr.next(&itr)) {
            for (uint8_t i = 0; i < itr.len && i < FIFO_OUTPUTS_MAX; i++) {
                printf("command from element %u, output %u, value %u: sending to core 1\n", itr.id, i, itr.pay[i]);
                push_to_core1(fifo_action_word(itr.id, i, itr.pay[i]));
            }
        }
    }
}

// Handle the complete frames in the client's receive buffer and keep any partial
// frame for the next segment. Returns false if the connection was closed.
static bool handle_frames(tcpclient *clientstate, err_t *result) {
    size_t used = 0;

    for (;;) {
        ws_frame f;
        ws_frame_status status = ws_parse_frame(clientstate->rx + used, clientstate->rx_len - used, WS_RX_BUF, &f);
        if (status == WS_FRAME_INCOMPLETE) break;
        if (status == WS_FRAME_TOO_BIG) {
            printf("websocket frame larger than %u bytes, closing\n", WS_RX_BUF);
            *result = tcp_client_close(clientstate);
            return false;
        }

        uint8_t *payload = clientstate->rx + used + f.header_len;
        size_t length = f.payload_len;
        if (f.masked) ws_unmask(payload, length, f.mask);
        used += f.header_len + length;

        switch (f.opcode) {
        case WS_OP_TEXT:
            printf("Text Message: %.*s\n", length, payload);
            break;
        case WS_OP_BINARY:
            handle_packet(payload, length);
            break;
        case WS_OP_CLOSE:
            printf("Client requested to close WebSocket\n");
            *result = tcp_client_close(clientstate);
            return false;
        case WS_OP_PING:
            printf("Ping received, sending Pong...\n");
            send_websocket_frame(clientstate, WS_OP_PONG, payload, length);
            break;
        default:
            printf("websocket opcode %X not supported, ignored\n", f.opcode);
            break;
        }
    }

    memmove(clientstate->rx, clientstate->rx + used, clientstate->rx_len - used);
    clientstate->rx_len -= used;
    return true;
}

err_t handle_websocket_msg(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    tcpclient *clientstate = (tcpclient*)arg;
    err_t result = ERR_OK;

    uint16_t total = p->tot_len;
    tcp_recved(tpcb, total);

    // Copy the segment into the receive buffer, handling frames as they complete.
    // A frame that fits the buffer always completes before the buffer is full.
    for (uint16_t offset = 0; offset < total; ) {
        size_t space = WS_RX_BUF - clientstate->rx_len;
        size_t n = total - offset < space ? total - offset : space;
        if (n == 0) {
            printf("websocket receive buffer full, closing\n");
            pbuf_free(p);
            return tcp_client_close(clientstate);
        }
        pbuf_copy_partial(p, clientstate->rx + clientstate->rx_len, n, offset);
        clientstate->rx_len += n;
        offset += n;

        if (!handle_frames(clientstate, &result)) {
            pbuf_free(p);
            return result;
        }
    }

    pbuf_free(p);
    return result;
}
