#ifndef ETHERNET_MANAGER_HPP
#define ETHERNET_MANAGER_HPP
// #include "esp_netif.h"

// extern esp_netif_t *eth_netif_spi;
void start_eth(void);
void get_eth_ip(char *ip);

#endif // ETHERNET_MANAGER_HPP
