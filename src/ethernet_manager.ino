// #include <stdio.h>
// #include <string.h>
// #include <stdint.h>
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "esp_netif.h"
// #include "esp_eth.h"
// #include "esp_event.h"
// #include "esp_log.h"
// #include "driver/gpio.h"
// #include "sdkconfig.h"
// #include "driver/spi_master.h"
// #include "ethernet_manager.hpp"

// static const char *ETHERNET_TAG = "eth_example";
// esp_netif_t *eth_netif_spi = NULL;


// typedef struct {
//     uint8_t spi_cs_gpio;
//     uint8_t int_gpio;
//     int8_t phy_reset_gpio;
//     uint8_t phy_addr;
// } spi_eth_module_config_t;

// static void eth_event_handler(void *arg, esp_event_base_t event_base,
//                               int32_t event_id, void *event_data)
// {
//     uint8_t mac_addr[6] = {0};
//     esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

//     switch (event_id) {
//     case ETHERNET_EVENT_CONNECTED:
//         esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
//         ESP_LOGI(ETHERNET_TAG, "Ethernet Link Up");
//         ESP_LOGI(ETHERNET_TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
//                  mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
//         break;
//     case ETHERNET_EVENT_DISCONNECTED:
//         ESP_LOGI(ETHERNET_TAG, "Ethernet Link Down");
//         break;
//     case ETHERNET_EVENT_START:
//         ESP_LOGI(ETHERNET_TAG, "Ethernet Started");
//         break;
//     case ETHERNET_EVENT_STOP:
//         ESP_LOGI(ETHERNET_TAG, "Ethernet Stopped");
//         break;
//     default:
//         break;
//     }
// }

// static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
//                                  int32_t event_id, void *event_data)
// {
//     ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
//     const esp_netif_ip_info_t *ip_info = &event->ip_info;

//     ESP_LOGI(ETHERNET_TAG, "Ethernet Got IP Address");
//     ESP_LOGI(ETHERNET_TAG, "~~~~~~~~~~~");
//     ESP_LOGI(ETHERNET_TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
//     ESP_LOGI(ETHERNET_TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
//     ESP_LOGI(ETHERNET_TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
//     ESP_LOGI(ETHERNET_TAG, "~~~~~~~~~~~");
// }

// void start_eth(void)
// {
//     ESP_LOGI(ETHERNET_TAG, "Initializing Ethernet...");

//     esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
//     esp_netif_config_t cfg_spi = {
//         .base = &esp_netif_config,
//         .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH
//     };
//     char if_key_str[10];
//     char if_desc_str[10];
//     strcpy(if_key_str, "ETH_SPI");
//     strcpy(if_desc_str, "eth");
//     esp_netif_config.if_key = if_key_str;
//     esp_netif_config.if_desc = if_desc_str;
//     esp_netif_config.route_prio = 30;
//     eth_netif_spi = esp_netif_new(&cfg_spi);

//     eth_mac_config_t mac_config_spi = ETH_MAC_DEFAULT_CONFIG();
//     eth_phy_config_t phy_config_spi = ETH_PHY_DEFAULT_CONFIG();

//     gpio_install_isr_service(0);

//     spi_device_handle_t spi_handle = NULL;
//     spi_bus_config_t buscfg = {
//         .mosi_io_num = 21,
//         .miso_io_num = 14,
//         .sclk_io_num = 13,
//         .quadwp_io_num = -1,
//         .quadhd_io_num = -1,
//     };
//     ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

//     spi_eth_module_config_t spi_eth_module_config = {
//         .spi_cs_gpio = 12,
//         .int_gpio = 47,
//         .phy_reset_gpio = 48,
//         .phy_addr = 1
//     };

//     esp_eth_mac_t *mac_spi;
//     esp_eth_phy_t *phy_spi;
//     esp_eth_handle_t eth_handle_spi = NULL;
//     spi_device_interface_config_t devcfg = {
//         .command_bits = 16,
//         .address_bits = 8,
//         .mode = 0,
//         .clock_speed_hz = 36 * 1000 * 1000,
//         .queue_size = 20
//     };

//     devcfg.spics_io_num = spi_eth_module_config.spi_cs_gpio;

//     ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &spi_handle));

//     eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(spi_handle);

//     w5500_config.int_gpio_num = spi_eth_module_config.int_gpio;
//     phy_config_spi.phy_addr = spi_eth_module_config.phy_addr;
//     phy_config_spi.reset_gpio_num = spi_eth_module_config.phy_reset_gpio;

//     mac_spi = esp_eth_mac_new_w5500(&w5500_config, &mac_config_spi);
//     phy_spi = esp_eth_phy_new_w5500(&phy_config_spi);

//     esp_eth_config_t eth_config_spi = ETH_DEFAULT_CONFIG(mac_spi, phy_spi);
//     ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config_spi, &eth_handle_spi));

//     // Read mac adress burned in EFUSE
//     uint8_t mac_address[6];
//     esp_read_mac(mac_address,ESP_MAC_WIFI_STA);
//     mac_address[6]+=3;
//     esp_eth_ioctl(eth_handle_spi, ETH_CMD_S_MAC_ADDR, mac_address);

//     ESP_ERROR_CHECK(esp_netif_attach(eth_netif_spi, esp_eth_new_netif_glue(eth_handle_spi)));

//     ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
//     ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

//     ESP_ERROR_CHECK(esp_eth_start(eth_handle_spi));

//     ESP_LOGI(ETHERNET_TAG, "Ethernet Initialization Complete");
// }
/* Ethernet Basic Example
   This example code is in the Public Domain (or CC0 licensed, at your option.)
   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
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

static const char *ETHERNET_TAG = "eth_example";
esp_netif_t *eth_netif_spi = { NULL };

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
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(ETHERNET_TAG, "Ethernet Link Down");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(ETHERNET_TAG, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(ETHERNET_TAG, "Ethernet Stopped");
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

void start_eth(void)
{
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

    // Init MAC and PHY configs to default
    eth_mac_config_t mac_config_spi = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config_spi = ETH_PHY_DEFAULT_CONFIG();

    // Install GPIO ISR handler to be able to service SPI Eth modlues interrupts
    gpio_install_isr_service(0);

    // Init SPI bus
    spi_device_handle_t spi_handle = { NULL };
    spi_bus_config_t buscfg = {
        .mosi_io_num = 21,
        .miso_io_num = 14,
        .sclk_io_num = 13,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

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
    esp_read_mac(mac_address,ESP_MAC_WIFI_STA);
    mac_address[6]+=3;
    esp_eth_ioctl(eth_handle_spi, ETH_CMD_S_MAC_ADDR, mac_address);

        // attach Ethernet driver to TCP/IP stack
        ESP_ERROR_CHECK(esp_netif_attach(eth_netif_spi, esp_eth_new_netif_glue(eth_handle_spi)));

    // Register user defined event handers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    /* start Ethernet driver state machine */
        ESP_ERROR_CHECK(esp_eth_start(eth_handle_spi));
}