#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "tcpserver.h"
#include "websockets.h"
#include "pico/multicore.h"
#include "ui_dist_index_html_gz.h"

#define TCP_PORT 80
#define DEBUG_printf printf
#define TEST_ITERATIONS 10
#define POLL_TIME_S 5

tcpserver* tcp_server_init(void) {
    tcpserver *state = calloc(1, sizeof(tcpserver));
    if (!state) {
        DEBUG_printf("failed to allocate state\n");
        return NULL;
    }
    return state;
}

tcpclient* tcp_client_init(void) {
    tcpclient *state = calloc(1, sizeof(tcpclient));
    if (!state) {
        DEBUG_printf("failed to allocate state\n");
        return NULL;
    }
    return state;
}

err_t tcp_client_close(void *arg) {
    tcpclient *clientstate = (tcpclient*)arg;

    tcpserver *server = clientstate->server;
    for (int i = 0; i < 10; i++) {
        if (server->clients[i] == clientstate) {
            server->clients[i] = NULL;
            break;
        }
    }

    err_t err = ERR_OK;
    if (clientstate->client_pcb != NULL) {
        tcp_arg(clientstate->client_pcb, NULL);
        tcp_poll(clientstate->client_pcb, NULL, 0);
        tcp_sent(clientstate->client_pcb, NULL);
        tcp_recv(clientstate->client_pcb, NULL);
        tcp_err(clientstate->client_pcb, NULL);
        err = tcp_close(clientstate->client_pcb);
        if (err != ERR_OK) {
            DEBUG_printf("close failed %d, calling abort\n", err);
            tcp_abort(clientstate->client_pcb);
            err = ERR_ABRT;
        }
        clientstate->client_pcb = NULL;
    }
    free(clientstate->rx);
    free(clientstate);

    printf("client closed\n");
    return err;
}

err_t tcp_server_close(void *arg) {
    tcpserver *state = (tcpserver*)arg;
    err_t err = ERR_OK;
    if (state->server_pcb) {
        tcp_arg(state->server_pcb, NULL);
        tcp_close(state->server_pcb);
        state->server_pcb = NULL;
    }
    return err;
}

err_t tcp_server_result(void *arg, int status) {
    tcpserver *state = (tcpserver*)arg;
    if (status == 0) {
        DEBUG_printf("test success\n");
    } else {
        DEBUG_printf("test failed %d\n", status);
    }
    state->complete = true;
    return tcp_server_close(arg);
}

err_t tcp_server_sent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    tcpserver *state = (tcpserver*)arg;
    DEBUG_printf("tcp_server_sent %u\n", len);
    state->sent_len += len;

    if (state->sent_len >= BUF_SIZE) {

        // We should get the data back from the client
        state->recv_len = 0;
        DEBUG_printf("Waiting for buffer from client\n");
    }

    return ERR_OK;
}

bool header_contains(const char *request, const char *header_name, const char *value) {
    const char *line = request;
    size_t header_name_len = strlen(header_name);
    while ((line = strstr(line, header_name)) != NULL) {
        if (line != request && *(line - 1) != '\n') {
            line += header_name_len;
            continue;
        }
        line += header_name_len;
        while (*line == ' ' || *line == ':' || *line == ',') line++;
        char value_copy[256];
        size_t i = 0;
        while (*line != '\r' && *line != '\n' && *line != '\0' && i < sizeof(value_copy) - 1) {
            value_copy[i++] = *line++;
        }
        value_copy[i] = '\0';
        char *token = strtok(value_copy, ",");
        while (token != NULL) {
            while (isspace((unsigned char)*token)) token++;
            char *end = token + strlen(token) - 1;
            while (end > token && isspace((unsigned char)*end)) *end-- = '\0';

            if (strcasecmp(token, value) == 0) {
                return true;
            }
            token = strtok(NULL, ",");
        }
    }
    return false;
}

err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    tcpclient *clientstate = (tcpclient*)arg;

    if (!p) {
        printf("Receiving, buffer is null\n");
        return tcp_client_close(clientstate);
    }

    // cyw43_arch_lwip_check();

    if (p->tot_len > 0) {
        if (!clientstate->upgraded) {
            printf("tcp_server_recv %d bytes, err %d\n", p->tot_len, err);
            static char buffer[1024];
            uint16_t copied = pbuf_copy_partial(p, buffer, sizeof(buffer) - 1, 0);
            buffer[copied] = '\0';  // Ensure null termination

            if (header_contains(buffer, "Upgrade", "websocket")
                && header_contains(buffer, "Connection", "Upgrade")) {

                return handle_websocket_req(clientstate, tpcb, p, err);
            } else {
                printf("%s\n",buffer);

                char header[256];  // Make sure this is large enough for your headers

                int header_len = snprintf(header, sizeof(header),
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html\r\n"
                    "Content-Encoding: gzip\r\n"
                    "Content-Length: %u\r\n"
                    "Connection: close\r\n"
                    "\r\n",
                    ui_dist_index_html_gz_len);

                // Send header
                err_t err = tcp_write(tpcb, header, header_len, TCP_WRITE_FLAG_COPY);
                if (err != ERR_OK) {
                    printf("Error writing header: %d\n", err);
                    return ERR_ABRT;
                }
            
                // Send gzipped body
                err = tcp_write(tpcb, ui_dist_index_html_gz, ui_dist_index_html_gz_len, TCP_WRITE_FLAG_COPY);
                if (err != ERR_OK) {
                    printf("Error writing gzipped content: %d\n", err);
                    return ERR_ABRT;
                }
            
                tcp_output(tpcb);

                // pbuf_free(p);
                // return tcp_client_close(clientstate);
            }
        } else {
            return handle_websocket_msg(clientstate, tpcb, p, err);
        }
    }

    return ERR_OK;
}

void tcp_server_err(void *arg, err_t err) {
    if (err != ERR_ABRT) {
        DEBUG_printf("tcp_client_err_fn %d\n", err);
        tcp_server_result(arg, err);
    }
}

err_t tcp_server_accept(void *arg, struct tcp_pcb *client_pcb, err_t err) {
    tcpserver *state = (tcpserver*)arg;
    if (err != ERR_OK || client_pcb == NULL) {
        DEBUG_printf("Failure in accept\n");
        tcp_server_result(arg, err);
        return ERR_VAL;
    }
    DEBUG_printf("Client connected\n");

    tcpclient *clientstate = tcp_client_init();
    if (!clientstate) {
        tcp_close(client_pcb);
        return ERR_MEM;
    }

    clientstate->client_pcb = client_pcb;
    clientstate->server = state;
    clientstate->upgraded = false;
    // clientstate->buffer = NULL; // Initialize client-specific data
    state->clients[0] = clientstate;

    // Associate per-client state with this client PCB
    tcp_arg(client_pcb, clientstate);

    // tcp_sent(client_pcb, tcp_server_sent);
    tcp_recv(client_pcb, tcp_server_recv);
    // tcp_poll(client_pcb, tcp_server_poll, POLL_TIME_S * 2);
    tcp_err(client_pcb, tcp_server_err);

    // return tcp_server_send_data(arg, state->client_pcb);
    return ERR_OK;
}

bool tcp_server_open(void *arg) {
    tcpserver *state = (tcpserver*)arg;
    DEBUG_printf("Starting server at %s on port %u\n", ip4addr_ntoa(netif_ip4_addr(netif_list)), TCP_PORT);

    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) {
        DEBUG_printf("failed to create pcb\n");
        return false;
    }

    err_t err = tcp_bind(pcb, NULL, TCP_PORT);
    if (err) {
        DEBUG_printf("failed to bind to port %u\n", TCP_PORT);
        return false;
    }

    state->server_pcb = tcp_listen_with_backlog(pcb, 1);
    if (!state->server_pcb) {
        DEBUG_printf("failed to listen\n");
        if (pcb) {
            tcp_close(pcb);
        }
        return false;
    }

    tcp_arg(state->server_pcb, state);
    tcp_accept(state->server_pcb, tcp_server_accept);

    return true;
}