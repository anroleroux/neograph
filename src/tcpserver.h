#ifndef TCPSERVER_H_INCLUDED
#define TCPSERVER_H_INCLUDED

#include "networking/dhcpserver.h"

#define BUF_SIZE 2048

typedef struct tcpserver {
    struct tcp_pcb *server_pcb;
    // struct tcp_pcb *client_pcb;
    struct tcpclient *clients[10];
    bool complete;
    // uint8_t buffer_sent[BUF_SIZE];
    // uint8_t buffer_recv[BUF_SIZE];
    int sent_len;
    int recv_len;
    
    dhcp_server_t dhcp_server;
    ip_addr_t gw;
} tcpserver;

typedef struct tcpclient {
    // struct tcp_pcb *server_pcb;
    struct tcp_pcb *client_pcb;
    tcpserver *server;
    void *cl;
    // char buffer [125];
    // bool complete;
    // uint8_t buffer_sent[BUF_SIZE];
    // uint8_t buffer_recv[BUF_SIZE];
    // int sent_len;
    // int recv_len;
    // int run_count;
    bool upgraded;
    uint8_t *rx;        // WebSocket receive buffer (WS_RX_BUF bytes), once upgraded
    size_t rx_len;
    const uint8_t *page;    // the part of the page not yet queued for sending
    size_t page_left;
} tcpclient;

tcpserver* tcp_server_init(void);
err_t tcp_server_close(void *arg);
err_t tcp_client_close(void *arg);
err_t tcp_server_result(void *arg, int status);
err_t tcp_server_sent(void *arg, struct tcp_pcb *tpcb, u16_t len);
err_t tcp_server_send_data(void *arg, struct tcp_pcb *tpcb);
err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
err_t tcp_server_poll(void *arg, struct tcp_pcb *tpcb);
void tcp_server_err(void *arg, err_t err);
err_t tcp_server_accept(void *arg, struct tcp_pcb *client_pcb, err_t err);
bool tcp_server_open(void *arg);
#endif