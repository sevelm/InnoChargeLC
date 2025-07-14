#ifndef ETHERNET_MANAGER_HPP
#define ETHERNET_MANAGER_HPP
#include "esp_netif.h"
typedef struct{
    uint8_t mac_addr[6];
    uint8_t ip_addr[4];
    uint8_t netmask[4];
    uint8_t gw[4];
    uint8_t dns1[4];
    uint8_t dns2[4];
} ethernet_start_config_t;

typedef enum {
    ETHERNET_STATUS_DISCONNECTED,
    ETHERNET_STATUS_CONNECTED,
    ETHERNET_STATUS_ERROR
} ethernet_connection_state_t;

typedef struct {
    bool is_enabled; // Ethernet is enabled
    uint8_t mac_addr[6]; // MAC address
    uint8_t ip_addr[4]; // IP address
    uint8_t netmask[4]; // Netmask
    uint8_t gw[4]; // Gateway
    uint8_t dns1[4]; // DNS1
    uint8_t dns2[4]; // DNS2
    ethernet_connection_state_t connection_state;
} ethernet_state_t;

/* ---------- API ------------------------------------------------- */
void start_eth(bool is_dhcp_enabled, ethernet_start_config_t * ethernet_start_config);
void get_eth_ip(char *ip);
void get_ethernet_state( ethernet_state_t *eth_status);
void stop_eth();
void restart_new_settings_eth();
void wifi_scan();
#endif // ETHERNET_MANAGER_HPP
