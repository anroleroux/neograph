#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/unique_id.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/apps/lwiperf.h"
#include "networking/dhcpserver.h"
#include "lwip/dhcp.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "wifi.h"
#include "config.h"
#include "protocol.h"

_Static_assert(NET_LEASE_FIRST == DHCPS_BASE_IP && NET_LEASE_COUNT == DHCPS_MAX_IP,
               "the DHCP server leases the addresses protocol.h keeps the access point off");

static void ip_from_bytes(ip4_addr_t *ip, const uint8_t a[4]) {
    IP4_ADDR(ip, a[0], a[1], a[2], a[3]);
}

// Connection retry parameters
#define MAX_RETRIES 5
#define RETRY_DELAY_MS 5000

void print_wifi_status(int status_code) {
    printf("WiFi status code: %d - ", status_code);
    switch(status_code) {
        case CYW43_LINK_DOWN:
            printf("Link down\n");
            break;
        case CYW43_LINK_JOIN:
            printf("Joining\n");
            break;
        case CYW43_LINK_NOIP:
            printf("Joined but no IP\n");
            break;
        case CYW43_LINK_UP:
            printf("Connected with IP\n");
            break;
        case CYW43_LINK_FAIL:
            printf("Connection failed\n");
            break;
        case CYW43_LINK_NONET:
            printf("Network not found\n");
            break;
        case CYW43_LINK_BADAUTH:
            printf("Authentication failed\n");
            break;
        default:
            printf("Unknown status\n");
    }
}

void create_wifi_access_point(const network_config nc) {

    printf("\nStarting access point...\n");
    // const char *ssid = "neoGraph-";
    // char ssid[16]; 
    // snprintf(ssid, sizeof(ssid), "neoGraph-%s", name);
    // const char *password = "neoGraph";

    cyw43_arch_enable_ap_mode(nc.ssid, nc.pass, CYW43_AUTH_WPA2_AES_PSK);

    printf("Access point started with SSID: %s\n", nc.ssid);
}

int create_network(tcpserver *state) {
    network_setup ns;
    memset(&ns, 0, sizeof ns);

    retrieve_networking_config(&ns);

    printf("\nDevice ID: ");
    for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++) {
        printf("%02X", ns.device.did[i]);
        if (i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES-1) printf(" ");
    }

    printf("\nDevice name: %s\n",ns.device.name);
    printf("Network name: %s\n",ns.network.name);
    printf("Network ssid: %s\n",ns.network.ssid);
    printf("Network opts: %d\n",ns.network.opts);

    if ((ns.network.opts&1) == 0) {
        printf("Create network access point\n");
        create_wifi_access_point(ns.network);

        // Its address, set or the default 192.168.4.1, on a /24. It is also the
        // gateway its DHCP server hands out.
        ip4_addr_t mask;
        mask.addr = PP_HTONL(CYW43_DEFAULT_IP_MASK);
        if (net_ap_address_usable(ns.network.ap_address)) {
            ip_from_bytes(&state->gw, ns.network.ap_address);
        } else if (net_address_usable(ns.network.ap_address)) {
            const uint8_t *a = ns.network.ap_address;
            printf("Access point address %u.%u.%u.%u is one its DHCP server leases; using the default\n",
                   a[0], a[1], a[2], a[3]);
        }
        netif_set_addr(&cyw43_state.netif[CYW43_ITF_AP], &state->gw, &mask, &state->gw);
        printf("Access point address: %s\n", ip4addr_ntoa(&state->gw));

        // Start the dhcp server
        dhcp_server_init(&state->dhcp_server, &state->gw, &mask);
    } else {
        printf("Connect to WiFi\n");
        return connect_to_wifi(ns.network);
    }
    return 0;
}

int connect_to_wifi(const network_config nw) {
    cyw43_arch_enable_sta_mode();

    printf("Connecting to WiFi, SSID: %s\n", nw.ssid);
    int result = cyw43_arch_wifi_connect_timeout_ms(nw.ssid, nw.pass, CYW43_AUTH_WPA2_AES_PSK, 10000);
    print_wifi_status(result);

    int retries = 0;
    while (result && retries < MAX_RETRIES) {
        printf("Connection failed, waiting %d ms before retrying...\n", RETRY_DELAY_MS);
        sleep_ms(RETRY_DELAY_MS);

        printf("Retrying connection (attempt %d of %d)...\n", retries + 1, MAX_RETRIES);
        result = cyw43_arch_wifi_connect_timeout_ms(nw.ssid, nw.pass, CYW43_AUTH_WPA2_AES_PSK, 10000);
        
        retries++;
    }
    
    if (result) {
        printf("Failed to connect to WiFi after %d attempts\n", MAX_RETRIES);
        // cyw43_arch_deinit();
        return 1;
    }
    print_wifi_status(result);

    if (net_address_usable(nw.static_address)) {
        // A fixed address. Joining got a DHCP lease first, which gives the
        // network's mask, and its gateway unless one is set.
        struct netif *sta = &cyw43_state.netif[CYW43_ITF_STA];
        ip4_addr_t ip, netmask, gateway;
        ip_from_bytes(&ip, nw.static_address);
        netmask = *netif_ip4_netmask(sta);
        if (ip4_addr_isany_val(netmask)) IP4_ADDR(&netmask, 255, 255, 255, 0);
        if (net_address_usable(nw.gateway)) ip_from_bytes(&gateway, nw.gateway);
        else gateway = *netif_ip4_gw(sta);

        dhcp_release_and_stop(sta);
        netif_set_addr(sta, &ip, &netmask, &gateway);

        printf("Static IP configuration:\n");
        printf("  IP: %s\n", ip4addr_ntoa(&ip));
        printf("  Netmask: %s\n", ip4addr_ntoa(&netmask));
        printf("  Gateway: %s\n", ip4addr_ntoa(&gateway));
    } else {
        printf("Connected to WiFi successfully!\n");
        ip4_addr_t gw = netif_list->gw;
        printf("Gateway address: %s\n", ip4addr_ntoa(&gw));
        
        printf("Ensuring DHCP is complete...\n");
        sleep_ms(2000);
        
        // Get and print the IP address
        ip4_addr_t ip = netif_list->ip_addr;
        printf("IP address: %s\n", ip4addr_ntoa(&ip));
        
        // Check if we have a valid IP (not 0.0.0.0)
        if (ip4_addr_isany_val(ip)) {
            printf("Error: No valid IP address assigned. DHCP may have failed.\n");
            
            // Try to manually renew DHCP
            printf("Attempting to renew DHCP lease...\n");
            dhcp_renew(netif_list);
            
            // Wait for DHCP to complete
            sleep_ms(5000);
            
            // Check IP again
            ip = netif_list->ip_addr;
            printf("IP address after DHCP renewal: %s\n", ip4addr_ntoa(&ip));
            
            if (ip4_addr_isany_val(ip)) {
                printf("Still no valid IP address. Please check your router settings.\n");
                return 2;
            }
        }
    }
    
    return 0;
}