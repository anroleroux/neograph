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
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(100));
        
        uint32_t value;
        if (multicore_fifo_pop_timeout_us(1000, &value)) {
            switch (value) {
            case FIFO_TAG_UPTIME:
                value = multicore_fifo_pop_blocking();
                for (int i = 0; i < 10; i++) {
                    if ((state->clients[i] != NULL) && (state->clients[i]->upgraded)) {
                        uint8_t buffer[124];
                        snprintf(buffer,124,"uptime %d", value);
                        send_websocket_message(state->clients[i], buffer);
                    }
                }
                break;
            case FIFO_TAG_TEMPERATURE:
                value = multicore_fifo_pop_blocking();
                float temperature;
                memcpy(&temperature, &value, sizeof(float));
                for (int i = 0; i < 10; i++) {
                    if ((state->clients[i] != NULL) && (state->clients[i]->upgraded)) {
                        uint8_t buffer[124];
                        snprintf(buffer,124,"core %.2f", temperature);
                        send_websocket_message(state->clients[i], buffer);
                    }
                }
                break;
            case FIFO_TAG_LED:
                value = multicore_fifo_pop_blocking();
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, value);
                break;
            }
        }
    }

    tcp_server_close(state);
    free(state);
    
    // This part will never be reached in this simple example
    cyw43_arch_deinit();
    return 0;
}