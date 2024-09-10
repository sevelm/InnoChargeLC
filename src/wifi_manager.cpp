#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_wifi.h>
#include <esp_mac.h>

#include "wifi_manager.hpp"

const char *WIFI_TAG = "WIFI MANAGER : ";

int retry_interval = 1000;
int max_retry = 0;
int retry_count = 0;

wifi_sta_state_t current_wifi_sta_state = {
    .connected = false,
    .ip = {0},
    .netmask = {0},
    .gateway = {0},
    .dns1 = {0},
    .dns2 = {0},
    .mac = {0}
};

wifi_scan_ap_data scanned_aps[MAXIMUM_AP];
uint16_t scanned_ap_count = 0;

const char* auth_mode_type(wifi_auth_mode_t auth_mode) {
    switch (auth_mode) {
        case WIFI_AUTH_OPEN:
            return "Open";
        case WIFI_AUTH_WEP:
            return "WEP";
        case WIFI_AUTH_WPA_PSK:
            return "WPA_PSK";
        case WIFI_AUTH_WPA2_PSK:
            return "WPA2_PSK";
        case WIFI_AUTH_WPA_WPA2_PSK:
            return "WPA_WPA2_PSK";
        case WIFI_AUTH_WPA3_PSK:
            return "WPA3_PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return "WPA2_WPA3_PSK";
        case WIFI_AUTH_WAPI_PSK:
            return "WAPI_PSK";
        default:
            return "Unknown";
    }
}

bool ssid_exists(const char* ssid, wifi_ap_record_t wifi_records[], int index) {
    for (int i = 0; i < index; i++) {
        if (strcmp((char *)wifi_records[i].ssid, ssid) == 0) {
            return true;  // SSID already exists
        }
    }
    return false;
}

void wifi_scan() {

    ESP_LOGI(WIFI_TAG, "Starting WiFi scan...");
    bool was_wifi_init = true;
    bool was_wifi_started = true;

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false
    };

    // Initialize and start WiFi if necessary
    esp_err_t err;
    do {
        err = esp_wifi_scan_start(&scan_config, true);
        if (err != ESP_OK) {
            if (err == ESP_ERR_WIFI_NOT_INIT) {
                ESP_LOGE(WIFI_TAG, "WiFi is not initialized. Initializing...");
                wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
                ESP_ERROR_CHECK(esp_wifi_init(&cfg));
                was_wifi_init = false;
            }
            else if (err == ESP_ERR_WIFI_NOT_STARTED) {
                ESP_LOGE(WIFI_TAG, "WiFi is not started. Starting...");
                ESP_ERROR_CHECK(esp_wifi_start());
                was_wifi_started = false;
            }
            else {
                ESP_LOGE(WIFI_TAG, "WiFi scan failed at start: %s", esp_err_to_name(err));
                return;
            }
        }
    } while (err != ESP_OK);

   wifi_ap_record_t wifi_records[MAXIMUM_AP];

    uint16_t max_records = MAXIMUM_AP;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&max_records, wifi_records));

    printf("Number of Access Points Found: %d\n", max_records);
    printf("\n");
    printf("               SSID              | Channel | RSSI |   Authentication Mode \n");
    printf("***************************************************************\n");

for (int i = 0; i < max_records; i++) {
    // Check if the SSID is already listed (to avoid duplicates)
    if (!ssid_exists((char *)wifi_records[i].ssid, wifi_records, i)) {
        // Store unique SSIDs in the "scanned_aps" variable
        strncpy(scanned_aps[i].ssid, (char *)wifi_records[i].ssid, 32);
        scanned_aps[i].ssid[32] = '\0';  // Ensure the SSID is null-terminated
        scanned_aps[i].rssi = wifi_records[i].rssi;
        scanned_aps[i].channel = wifi_records[i].primary;
        scanned_aps[i].authmode = wifi_records[i].authmode;

        // Print from the "scanned_aps" variable immediately
        printf("%32s | %7d | %4d | %12s\n", 
            scanned_aps[i].ssid, 
            scanned_aps[i].channel, 
            scanned_aps[i].rssi, 
            auth_mode_type(scanned_aps[i].authmode));
    }
}

    printf("***************************************************************\n");





}








// Event Handler
static void wifi_sta_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(WIFI_TAG, "WIFI_EVENT_STA_START");
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED)
    {
        ESP_LOGI(WIFI_TAG, "WIFI_EVENT_STA_CONNECTED");
        current_wifi_sta_state.connected = true;
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(WIFI_TAG, "WIFI_EVENT_STA_DISCONNECTED");
        current_wifi_sta_state.connected = false;
        uint8_t empty_ip[4] = {0};
        memcpy(current_wifi_sta_state.ip, empty_ip, 4);
        memcpy(current_wifi_sta_state.netmask, empty_ip, 4);
        memcpy(current_wifi_sta_state.gateway, empty_ip, 4);
        memcpy(current_wifi_sta_state.dns1, empty_ip, 4);
        memcpy(current_wifi_sta_state.dns2, empty_ip, 4);

        if(max_retry > 0 && retry_count >= max_retry)
        {
            ESP_LOGI(WIFI_TAG, "Max retry reached. Stopping wifi manager.");
        }
        else
        {
            retry_count++;
            ESP_LOGI(WIFI_TAG, "Retrying in %d ms", retry_interval);
            vTaskDelay(retry_interval / portTICK_PERIOD_MS);
            esp_wifi_connect();
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(WIFI_TAG, "IP_EVENT_STA_GOT_IP :" IPSTR, IP2STR(&event->ip_info.ip));
        retry_count = 0;
        // Set IP details to state variables
        memcpy(current_wifi_sta_state.ip, &event->ip_info.ip, 4);
        memcpy(current_wifi_sta_state.netmask, &event->ip_info.netmask, 4);
        memcpy(current_wifi_sta_state.gateway, &event->ip_info.gw, 4);
        esp_netif_dns_info_t dns_info;
        esp_netif_get_dns_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), ESP_NETIF_DNS_MAIN, &dns_info);
        memcpy(current_wifi_sta_state.dns1, &dns_info.ip.u_addr.ip4, 4);
        esp_netif_get_dns_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), ESP_NETIF_DNS_BACKUP, &dns_info);
        memcpy(current_wifi_sta_state.dns2, &dns_info.ip.u_addr.ip4, 4);
    }
}


// Start WiFi and create network interface
void wifi_init_sta(wifi_sta_start_config_t *config) {
    ESP_LOGI(WIFI_TAG, "Initializing wifi station");

    // Create the network interface only if it does not already exist
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        esp_netif_create_default_wifi_sta();
    } else {
        ESP_LOGW(WIFI_TAG, "WiFi STA interface already exists");
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_sta_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_sta_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold={
                .authmode = WIFI_AUTH_OPEN,
            },
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    strcpy((char *)wifi_config.sta.ssid, config->ssid);
    strcpy((char *)wifi_config.sta.password, config->passphrase);

    strcpy(current_wifi_sta_state.ssid, config->ssid);
    strcpy(current_wifi_sta_state.passphrase, config->passphrase);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    memcpy(current_wifi_sta_state.mac, mac, 6);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}


// Stop WiFi and release network interface
void wifi_stop_sta() {
    ESP_LOGI(WIFI_TAG, "Stopping wifi station");

    // Stop WiFi
    ESP_ERROR_CHECK(esp_wifi_stop());

    // Deinitialize WiFi
    ESP_ERROR_CHECK(esp_wifi_deinit());

    // Remove the WiFi network interface
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        ESP_LOGI(WIFI_TAG, "Destroying existing WiFi STA interface");
        esp_netif_destroy(netif);
    } else {
        ESP_LOGW(WIFI_TAG, "WiFi STA interface not found during stop");
    }
}



void get_wifi_sta_state(wifi_sta_state_t *state)
{
    memcpy(state, &current_wifi_sta_state, sizeof(wifi_sta_state_t));
}
