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
        // and the ADC inputs report several times a second. While a keystroke is
        // being typed, 1 ms: the PC takes a key down or up each millisecond.
        bool typing = keyboard_task();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(typing ? 1 : 10));

        // Each message from core 1 is a tag and a value (PROTOCOL.md, "Core 1 →
        // core 0"). Take all that are waiting.
        while (multicore_fifo_rvalid()) {
            uint32_t tag = multicore_fifo_pop_blocking();
            uint32_t value = multicore_fifo_pop_blocking();

            char text[32] = "";
            switch (tag) {
            case FIFO_TAG_UPTIME:
                snprintf(text, sizeof text, "uptime %u", (unsigned)value);
                break;
            case FIFO_TAG_TEMPERATURE: {
                float temperature;
                memcpy(&temperature, &value, sizeof(float));
                snprintf(text, sizeof text, "core %.2f", temperature);
                break;
            }
            case FIFO_TAG_ADC:
                snprintf(text, sizeof text, "adc %u %u", (unsigned)fifo_adc_channel(value), (unsigned)fifo_adc_raw(value));
                break;
            case FIFO_TAG_LED:
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, value);
                break;
            case FIFO_TAG_KEYBOARD:
                keyboard_press(value);
                break;
            }
            if (!text[0]) continue;

            for (int i = 0; i < 10; i++) {
                if ((state->clients[i] != NULL) && (state->clients[i]->upgraded)) {
                    send_websocket_message(state->clients[i], text);
                }
            }
        }
    }

    tcp_server_close(state);
    free(state);
    
    // This part will never be reached in this simple example
    cyw43_arch_deinit();
    return 0;
}