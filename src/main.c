#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/cyw43_arch.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/apps/lwiperf.h"
#include "lwip/dhcp.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "wifi.h"
#include "tcpserver.h"
#include "websockets.h"
#include "config.h"
#include "hwman.h"
#include "keyboard.h"
#include "protocol.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

static void send_to_clients(tcpserver *state, const char *text) {
    for (int i = 0; i < 10; i++) {
        if ((state->clients[i] != NULL) && (state->clients[i]->upgraded)) {
            send_websocket_message(state->clients[i], text);
        }
    }
}

// ── Signal telemetry ──
// Every SIGNAL_REPORT_MS, each signal of the program core 1 runs whose value has
// changed is sent to the browser as "sig <id> <output> <value>", and every one
// at least each SIGNAL_REFRESH_MS, so a browser that connects learns them all
// (PROTOCOL.md, "Telemetry text frames").
#define SIGNAL_REPORT_MS    100
#define SIGNAL_REFRESH_MS   1000

static void report_signals(tcpserver *state) {
    static absolute_time_t next_report, next_refresh;      // zero: at once
    static uint32_t generation = UINT32_MAX;
    static float reported[PROG_SIGNALS_MAX];

    if (!time_reached(next_report)) return;
    next_report = make_timeout_time_ms(SIGNAL_REPORT_MS);
    bool refresh = time_reached(next_refresh);
    if (refresh) next_refresh = make_timeout_time_ms(SIGNAL_REFRESH_MS);

    uint32_t now_generation;
    const hw_program *p = program_running(&now_generation);
    bool fresh = now_generation != generation;     // a new program: send every signal
    generation = now_generation;

    for (uint8_t i = 0; i < p->signal_count; i++) {
        float value = p->signals[i].value;
        if (!refresh && !fresh && value == reported[i]) continue;
        reported[i] = value;
        char text[40];
        snprintf(text, sizeof text, "sig %u %u %.4g", p->signals[i].id, p->signals[i].output, (double)value);
        send_to_clients(state, text);
    }
}

int main() {
    stdio_init_all();
    
    // Allow time for serial connection to establish
    sleep_ms(3000);

    printf("       ------------   --     --->\n");
    printf("      |  neoGraph  | |  |   |    \n");
    printf("------              -    ---     \n");
    printf("  on Raspberry Pi Pico 2 W\n");
    printf("\nSDK version: %s\n", PICO_SDK_VERSION_STRING);
    
    printf("Initializing CYW43...\n");
    if (cyw43_arch_init()) {
        printf("Failed to initialize CYW43 architecture\n");
        return 1;
    }
    
    printf("CYW43 initialized successfully\n");
    printf("System clock: %u Hz\n", clock_get_hz(clk_sys));
    
    printf("\nStarting networking\n");
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

    tcpserver *state = tcp_server_init();
    if (!state) {
        printf("\nERROR failed to allocate server state\n");
        return 1;
    }
    state->gw.addr = PP_HTONL(CYW43_DEFAULT_IP_AP_ADDRESS);

    

    int err = create_network(state);
    if (err != 0) {
        cyw43_arch_deinit();
        return 1;
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
    
    printf("\n(core0) launching core1\n");
    multicore_launch_core1(core1_worker);

    // The stored graph runs from boot: a browser that connects later is sent the
    // config, but core 1 is not set up again (PROTOCOL.md, "Session").
    apply_stored_config();
    
    if (!tcp_server_open(state)) {
        tcp_server_result(state, -1);
        return 1;
    }
    while(!state->complete) {
        // // Check connection status periodically
        // int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
        // printf("Current status: ");
        // print_wifi_status(status);
        
        // // Refresh IP address info
        // ip = netif_list->ip_addr;
        // printf("Current IP: %s\n", ip4addr_ntoa(&ip));

        cyw43_arch_poll();
        // Not longer than 10 ms: core 1 waits while its FIFO to core 0 is full,
        // and signals are reported several times a second. While a keystroke is
        // being typed, 1 ms: the PC takes a key down or up each millisecond.
        bool typing = keyboard_task();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(typing ? 1 : 10));

        // Each message from core 1 is a tag and a value (PROTOCOL.md, "Core 1 →
        // core 0"). Take all that are waiting.
        // Some may have been taken while core 0 waited to send (push_to_core1).
        uint32_t tag, value;
        while (take_from_core1(&tag, &value)) {

            char text[32] = "";
            switch (tag) {
            case FIFO_TAG_UPTIME:
                snprintf(text, sizeof text, "uptime %u", (unsigned)value);
                break;
            case FIFO_TAG_LED:
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, value);
                break;
            case FIFO_TAG_KEYBOARD:
                keyboard_press(value);
                break;
            }
            if (text[0]) send_to_clients(state, text);
        }

        report_signals(state);
    }

    tcp_server_close(state);
    free(state);
    
    // This part will never be reached in this simple example
    cyw43_arch_deinit();
    return 0;
}