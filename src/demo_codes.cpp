#include  "Arduino.h"
#include <esp_http_client.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"

#include "ethernet_manager.hpp"
#include "wifi_manager.hpp"
#include "demo_codes.hpp"

const char *ping_host = "http://www.google.com";
const char *DEMO_TAG = "Demo Task : ";

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(DEMO_TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(DEMO_TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(DEMO_TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            // ESP_LOGI(DEMO_TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            // ESP_LOGI(DEMO_TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(DEMO_TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(DEMO_TAG, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}


void demo_monitoring_task(void *args){

    while(1){
        ethernet_state_t eth_status;
        get_ethernet_state(&eth_status);
        printf("\n\n\n__________________________________________________________________");
        ESP_LOGI(DEMO_TAG, "Ethernet Connection Status: %d", eth_status.connection_state);
        ESP_LOGI(DEMO_TAG, "Ethernet MAC Address: %02x:%02x:%02x:%02x:%02x:%02x", eth_status.mac_addr[0], eth_status.mac_addr[1], eth_status.mac_addr[2], eth_status.mac_addr[3], eth_status.mac_addr[4], eth_status.mac_addr[5]);
        ESP_LOGI(DEMO_TAG, "Ethernet IP Address: %d.%d.%d.%d", eth_status.ip_addr[0], eth_status.ip_addr[1], eth_status.ip_addr[2], eth_status.ip_addr[3]);
        ESP_LOGI(DEMO_TAG, "Ethernet Netmask: %d.%d.%d.%d", eth_status.netmask[0], eth_status.netmask[1], eth_status.netmask[2], eth_status.netmask[3]);
        ESP_LOGI(DEMO_TAG, "Ethernet Gateway: %d.%d.%d.%d", eth_status.gw[0], eth_status.gw[1], eth_status.gw[2], eth_status.gw[3]);
        ESP_LOGI(DEMO_TAG, "Ethernet DNS1: %d.%d.%d.%d", eth_status.dns1[0], eth_status.dns1[1], eth_status.dns1[2], eth_status.dns1[3]);
        ESP_LOGI(DEMO_TAG, "Ethernet DNS2: %d.%d.%d.%d", eth_status.dns2[0], eth_status.dns2[1], eth_status.dns2[2], eth_status.dns2[3]);
        printf("__________________________________________________________________\n\n\n");

        wifi_sta_state_t wifi_status;
        get_wifi_sta_state(&wifi_status);
        printf("\n\n\n__________________________________________________________________");
        ESP_LOGI(DEMO_TAG, "Wifi Connection Status: %d", wifi_status.connected);
        ESP_LOGI(DEMO_TAG, "Wifi SSID: %s", wifi_status.ssid);
        ESP_LOGI(DEMO_TAG, "Wifi IP Address: %d.%d.%d.%d", wifi_status.ip[0], wifi_status.ip[1], wifi_status.ip[2], wifi_status.ip[3]);
        ESP_LOGI(DEMO_TAG, "Wifi Netmask: %d.%d.%d.%d", wifi_status.netmask[0], wifi_status.netmask[1], wifi_status.netmask[2], wifi_status.netmask[3]);
        ESP_LOGI(DEMO_TAG, "Wifi Gateway: %d.%d.%d.%d", wifi_status.gateway[0], wifi_status.gateway[1], wifi_status.gateway[2], wifi_status.gateway[3]);
        ESP_LOGI(DEMO_TAG, "Wifi DNS1: %d.%d.%d.%d", wifi_status.dns1[0], wifi_status.dns1[1], wifi_status.dns1[2], wifi_status.dns1[3]);
        ESP_LOGI(DEMO_TAG, "Wifi DNS2: %d.%d.%d.%d", wifi_status.dns2[0], wifi_status.dns2[1], wifi_status.dns2[2], wifi_status.dns2[3]);
        ESP_LOGI(DEMO_TAG, "Wifi MAC Address: %02x:%02x:%02x:%02x:%02x:%02x", wifi_status.mac[0], wifi_status.mac[1], wifi_status.mac[2], wifi_status.mac[3], wifi_status.mac[4], wifi_status.mac[5]);
        printf("__________________________________________________________________\n\n\n");
        
        
        
        vTaskDelay(10000 / portTICK_PERIOD_MS);

        
        // check if internet is working by pinging google.com
        esp_http_client_config_t config = {
            .url = ping_host,
            .event_handler = _http_event_handler,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            ESP_LOGI(DEMO_TAG, "HTTP GET Status = %d, content_length = %d",
                     esp_http_client_get_status_code(client),
                     esp_http_client_get_content_length(client));
        } else {
            ESP_LOGE(DEMO_TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
        }
        esp_http_client_cleanup(client);

        
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}