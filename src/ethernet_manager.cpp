
/* Ethernet Basic Example
   This example code is in the Public Domain (or CC0 licensed, at your option.)
   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "driver/spi_master.h"
#include "ethernet_manager.hpp"

#include "AA_globals.h"

static const char *ETHERNET_TAG = "eth_example";
esp_netif_t *eth_netif_spi = { NULL };

ethernet_state_t current_ethernet_status = {
    .is_enabled = false,
    .mac_addr = {0},
    .ip_addr = {0},
    .netmask = {0},
    .gw = {0},
    .dns1 = {0},
    .dns2 = {0},
    .connection_state = ETHERNET_STATUS_DISCONNECTED
};


// #define INIT_SPI_ETH_MODULE_CONFIG(eth_module_config, num)                                      \
//     do {                                                                                        \
//         eth_module_config[num].spi_cs_gpio = CONFIG_EXAMPLE_ETH_SPI_CS ##num## _GPIO;           \
//         eth_module_config[num].int_gpio = CONFIG_EXAMPLE_ETH_SPI_INT ##num## _GPIO;             \
//         eth_module_config[num].phy_reset_gpio = CONFIG_EXAMPLE_ETH_SPI_PHY_RST ##num## _GPIO;   \
//         eth_module_config[num].phy_addr = CONFIG_EXAMPLE_ETH_SPI_PHY_ADDR ##num;                \
//     } while(0)

typedef struct {
    uint8_t spi_cs_gpio;
    uint8_t int_gpio;
    int8_t phy_reset_gpio;
    uint8_t phy_addr;
}spi_eth_module_config_t;

/** Event handler for Ethernet events */
static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    /* we can get the ethernet driver handle from event data */
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(ETHERNET_TAG, "Ethernet Link Up");
        ESP_LOGI(ETHERNET_TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        current_ethernet_status.connection_state = ETHERNET_STATUS_CONNECTED;
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(ETHERNET_TAG, "Ethernet Link Down");
        current_ethernet_status.connection_state = ETHERNET_STATUS_DISCONNECTED;
        {
            uint8_t empty_ip[4] = {0};
            memcpy(current_ethernet_status.ip_addr, empty_ip, 4);
            memcpy(current_ethernet_status.netmask, empty_ip, 4);
            memcpy(current_ethernet_status.gw, empty_ip, 4);
            memcpy(current_ethernet_status.dns1, empty_ip, 4);
            memcpy(current_ethernet_status.dns2, empty_ip, 4);
        }
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(ETHERNET_TAG, "Ethernet Started");
        current_ethernet_status.is_enabled = true;
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(ETHERNET_TAG, "Ethernet Stopped");
        current_ethernet_status.is_enabled = false;
        break;
    default:
        break;
    }
}


/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(ETHERNET_TAG, "Ethernet Got IP Address");
    ESP_LOGI(ETHERNET_TAG, "~~~~~~~~~~~");
    ESP_LOGI(ETHERNET_TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(ETHERNET_TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(ETHERNET_TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(ETHERNET_TAG, "~~~~~~~~~~~");
    // set Ip address to current_ethernet_status
    memcpy(current_ethernet_status.ip_addr, &ip_info->ip.addr, 4);
    // set netmask to current_ethernet_status
    memcpy(current_ethernet_status.netmask, &ip_info->netmask.addr, 4);
    // set gateway to current_ethernet_status
    memcpy(current_ethernet_status.gw, &ip_info->gw.addr, 4);
    // Also get and set DNS1 and DNS2
    esp_netif_dns_info_t dns_info;
    esp_netif_get_dns_info(eth_netif_spi, ESP_NETIF_DNS_MAIN, &dns_info);
    memcpy(current_ethernet_status.dns1, &dns_info.ip.u_addr.ip4.addr, 4);
    esp_netif_get_dns_info(eth_netif_spi, ESP_NETIF_DNS_BACKUP, &dns_info);
    memcpy(current_ethernet_status.dns2, &dns_info.ip.u_addr.ip4.addr, 4);
    
}


void refresh_ethernet_status(){
    current_ethernet_status.is_enabled = esp_netif_is_netif_up(eth_netif_spi);
    if(current_ethernet_status.is_enabled){
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(eth_netif_spi, &ip_info) == ESP_OK) {
            current_ethernet_status.connection_state = ETHERNET_STATUS_CONNECTED;
            memcpy(current_ethernet_status.ip_addr, &ip_info.ip.addr, 4);
            memcpy(current_ethernet_status.netmask, &ip_info.netmask.addr, 4);
            memcpy(current_ethernet_status.gw, &ip_info.gw.addr, 4);
            esp_netif_dns_info_t dns_info;
            esp_netif_get_dns_info(eth_netif_spi, ESP_NETIF_DNS_MAIN, &dns_info);
            memcpy(current_ethernet_status.dns1, &dns_info.ip.u_addr.ip4.addr, 4);

            esp_netif_get_dns_info(eth_netif_spi, ESP_NETIF_DNS_BACKUP, &dns_info);
            memcpy(current_ethernet_status.dns2, &dns_info.ip.u_addr.ip4.addr, 4);
        }       
    }
}

void get_ethernet_state( ethernet_state_t *eth_status){
    memcpy(eth_status, &current_ethernet_status, sizeof(ethernet_state_t));
}

void get_eth_ip(char *ip)
{
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(eth_netif_spi, &ip_info) == ESP_OK) {
        sprintf(ip, IPSTR, IP2STR(&ip_info.ip));
    } else {
        ESP_LOGE(ETHERNET_TAG, "Failed to get IP address of SPI Ethernet");
        strcpy(ip, "0.0.0.0");
    }
}

void set_eth_dns(char * dns1, char * dns2){
    esp_netif_dns_info_t dns;
    esp_netif_str_to_ip4(dns1, &dns.ip.u_addr.ip4);
    dns.ip.type = IPADDR_TYPE_V4;
    esp_netif_set_dns_info(eth_netif_spi, ESP_NETIF_DNS_MAIN, &dns);
    esp_netif_str_to_ip4(dns2, &dns.ip.u_addr.ip4);
    dns.ip.type = IPADDR_TYPE_V4;
    esp_netif_set_dns_info(eth_netif_spi, ESP_NETIF_DNS_BACKUP, &dns);
}

bool is_spi_bus_initialize = false;

void start_eth(bool is_dhcp_enabled, ethernet_start_config_t *ethernet_start_config){
    if(is_dhcp_enabled){
        ESP_LOGI(ETHERNET_TAG, "Starting Ethernet with DHCP enabled");
        // Create instance(s) of esp-netif for SPI Ethernet(s)
        esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
        esp_netif_config_t cfg_spi = {
            .base = &esp_netif_config,
            .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH
        };
        char if_key_str[10];
        char if_desc_str[10];
        char num_str[3];
        strcpy(if_key_str, "ETH_SPI");
        strcpy(if_desc_str, "eth");
        esp_netif_config.if_key = if_key_str;
        esp_netif_config.if_desc = if_desc_str;
        esp_netif_config.route_prio = 30;
        eth_netif_spi = esp_netif_new(&cfg_spi);
    }
    else{
        ESP_LOGI(ETHERNET_TAG, "Starting Ethernet with DHCP disabled");
        char ip_str[16];
        esp_netif_ip_info_t my_static_ip;
        if(ethernet_start_config == NULL){
            ESP_LOGE(ETHERNET_TAG, "Ethernet start configuration is NULL");
            return;
        }
        sprintf(ip_str, "%d.%d.%d.%d", ethernet_start_config->ip_addr[0], ethernet_start_config->ip_addr[1], ethernet_start_config->ip_addr[2], ethernet_start_config->ip_addr[3]);
        esp_netif_str_to_ip4(ip_str, &my_static_ip.ip);
        sprintf(ip_str, "%d.%d.%d.%d", ethernet_start_config->netmask[0], ethernet_start_config->netmask[1], ethernet_start_config->netmask[2], ethernet_start_config->netmask[3]);
        esp_netif_str_to_ip4(ip_str, &my_static_ip.netmask);
        sprintf(ip_str, "%d.%d.%d.%d", ethernet_start_config->gw[0], ethernet_start_config->gw[1], ethernet_start_config->gw[2], ethernet_start_config->gw[3]);
        esp_netif_str_to_ip4(ip_str, &my_static_ip.gw);

        esp_netif_inherent_config_t esp_netif_static_ip_config = {
            .flags = (ESP_NETIF_FLAG_AUTOUP),
            .ip_info = &my_static_ip,
            .get_ip_event = IP_EVENT_ETH_GOT_IP,
            .lost_ip_event = 0,
            .if_key = "ETH_SPI",
            .if_desc = "eth",
            .route_prio = 30
        };
        // disable DHCP Client
        esp_netif_static_ip_config.flags = (esp_netif_flags_t) (esp_netif_static_ip_config.flags & ~ ESP_NETIF_DHCP_CLIENT);
        //  Disable DHCP Server
        esp_netif_static_ip_config.flags = (esp_netif_flags_t) (esp_netif_static_ip_config.flags & ~ ESP_NETIF_DHCP_SERVER);
        esp_netif_config_t cfg_spi = {
            .base = &esp_netif_static_ip_config,
            .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH
        };
        eth_netif_spi = esp_netif_new(&cfg_spi);

        // also copy the static IP configuration to current_ethernet_status
        memcpy(current_ethernet_status.ip_addr, ethernet_start_config->ip_addr, 4);
        memcpy(current_ethernet_status.netmask, ethernet_start_config->netmask, 4);
        memcpy(current_ethernet_status.gw, ethernet_start_config->gw, 4);
        // and dns details
        memcpy(current_ethernet_status.dns1, ethernet_start_config->dns1, 4);
        memcpy(current_ethernet_status.dns2, ethernet_start_config->dns2, 4);
        
    }

    // Init MAC and PHY configs to default
    eth_mac_config_t mac_config_spi = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config_spi = ETH_PHY_DEFAULT_CONFIG();

    // Init SPI bus
    spi_device_handle_t spi_handle = { NULL };
    spi_bus_config_t buscfg = {
        .mosi_io_num = 21,
        .miso_io_num = 14,
        .sclk_io_num = 13,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    if (!is_spi_bus_initialize) {
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    // Install GPIO ISR handler to be able to service SPI Eth modlues interrupts
        gpio_install_isr_service(0);
        is_spi_bus_initialize = true;
    }

    // Init specific SPI Ethernet module configuration from Kconfig (CS GPIO, Interrupt GPIO, etc.)
    spi_eth_module_config_t spi_eth_module_config={
        .spi_cs_gpio = 12,
        .int_gpio = 47,
        .phy_reset_gpio = 48,
        .phy_addr = 1
    };
    // Configure SPI interface and Ethernet driver for specific SPI module
    esp_eth_mac_t *mac_spi;
    esp_eth_phy_t *phy_spi;
    esp_eth_handle_t eth_handle_spi = { NULL };
    spi_device_interface_config_t devcfg = {
        .command_bits = 16, // Actually it's the address phase in W5500 SPI frame
        .address_bits = 8,  // Actually it's the control phase in W5500 SPI frame
        .mode = 0,
        .clock_speed_hz = 36 * 1000 * 1000,
        .queue_size = 20
    };

        // Set SPI module Chip Select GPIO
        devcfg.spics_io_num = spi_eth_module_config.spi_cs_gpio;

        ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &spi_handle));
        // w5500 ethernet driver is based on spi driver
        eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(spi_handle);

        // Set remaining GPIO numbers and configuration used by the SPI module
        w5500_config.int_gpio_num = spi_eth_module_config.int_gpio;
        phy_config_spi.phy_addr = spi_eth_module_config.phy_addr;
        phy_config_spi.reset_gpio_num = spi_eth_module_config.phy_reset_gpio;

        mac_spi = esp_eth_mac_new_w5500(&w5500_config, &mac_config_spi);
        phy_spi = esp_eth_phy_new_w5500(&phy_config_spi);

        esp_eth_config_t eth_config_spi = ETH_DEFAULT_CONFIG(mac_spi, phy_spi);
        ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config_spi, &eth_handle_spi));

            // Read mac adress burned in EFUSE
    uint8_t mac_address[6];
    esp_read_mac(mac_address,ESP_MAC_ETH);
    esp_eth_ioctl(eth_handle_spi, ETH_CMD_S_MAC_ADDR, mac_address);
    memcpy(current_ethernet_status.mac_addr, mac_address, 6);
        // attach Ethernet driver to TCP/IP stack
        ESP_ERROR_CHECK(esp_netif_attach(eth_netif_spi, esp_eth_new_netif_glue(eth_handle_spi)));

    // Register user defined event handers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    if(!is_dhcp_enabled){
        // Set DNS Info for static IP
        char dns1_str[16];
        char dns2_str[16];
        sprintf(dns1_str, "%d.%d.%d.%d", ethernet_start_config->dns1[0], ethernet_start_config->dns1[1], ethernet_start_config->dns1[2], ethernet_start_config->dns1[3]);
        sprintf(dns2_str, "%d.%d.%d.%d", ethernet_start_config->dns2[0], ethernet_start_config->dns2[1], ethernet_start_config->dns2[2], ethernet_start_config->dns2[3]);
        set_eth_dns(dns1_str, dns2_str);
    }

    /* start Ethernet driver state machine */
        ESP_ERROR_CHECK(esp_eth_start(eth_handle_spi));
}



    void stop_eth() {
        if (eth_netif_spi != NULL) {
            esp_netif_action_stop(eth_netif_spi, NULL, 0, NULL);
            esp_netif_destroy(eth_netif_spi);
            eth_netif_spi = NULL;
        }
            // Deinitialize SPI bus
        //    ESP_ERROR_CHECK(spi_bus_free(SPI2_HOST));

            // Deinitialize GPIO ISR service
        //    gpio_uninstall_isr_service();
            ESP_LOGI(ETHERNET_TAG, "Ethernet stopped");
    }

    void restart_new_settings_eth() {
            ethernet_state_t eth_status;
            eth_status.is_enabled = true;
            preferences.getBytes("eth_ip_addr", eth_status.ip_addr, sizeof(eth_status.ip_addr));
            preferences.getBytes("eth_netmask", eth_status.netmask, sizeof(eth_status.netmask));
            preferences.getBytes("eth_gw", eth_status.gw, sizeof(eth_status.gw));
            preferences.getBytes("eth_dns1", eth_status.dns1, sizeof(eth_status.dns1));
            preferences.getBytes("eth_dns2", eth_status.dns2, sizeof(eth_status.dns2));
            bool eth_static = preferences.getBool("ethStatic", true);

            if (eth_static) {
                    // Configure static Ethernet settings
                    ethernet_start_config_t eth_config = {
                    // .mac_addr = {eth_status.mac_addr[0], eth_status.mac_addr[1], eth_status.mac_addr[2], eth_status.mac_addr[3], eth_status.mac_addr[4], eth_status.mac_addr[5]},
                        .ip_addr = {eth_status.ip_addr[0], eth_status.ip_addr[1], eth_status.ip_addr[2], eth_status.ip_addr[3]},
                        .netmask = {eth_status.netmask[0], eth_status.netmask[1], eth_status.netmask[2], eth_status.netmask[3]},
                        .gw = {eth_status.gw[0], eth_status.gw[1], eth_status.gw[2], eth_status.gw[3]},
                        .dns1 = {eth_status.dns1[0], eth_status.dns1[1], eth_status.dns1[2], eth_status.dns1[3]},
                        .dns2 = {eth_status.dns2[0], eth_status.dns2[1], eth_status.dns2[2], eth_status.dns2[3]}
                        };
                        start_eth(false, &eth_config); // Start Ethernet with DHCP Client disabled
                } else {
                    start_eth(true, NULL); // Start Ethernet with DHCP Client enabled
                    };
        ESP_LOGI(ETHERNET_TAG, "Ethernet Restart with new settings");
    }
