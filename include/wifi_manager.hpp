#ifndef WIFI_MANAGER_HPP
#define WIFI_MANAGER_HPP
typedef struct {
    char ssid[64];
    char passphrase[64];
    int retry_interval; // in ms
    int max_retry; // -1 for infinite
} wifi_sta_start_config_t;

typedef struct {
    char ssid[64];
    char passphrase[64];
    bool connected;
    uint8_t ip[4];
    uint8_t netmask[4];
    uint8_t gateway[4];
    uint8_t dns1[4];
    uint8_t dns2[4];
    uint8_t mac[6];
} wifi_sta_state_t;

// Wifi Scan
#define MAXIMUM_AP 20
struct wifi_scan_ap_data {
    char ssid[33];
    int32_t rssi;
    uint8_t channel;
    wifi_auth_mode_t authmode;
};
extern wifi_scan_ap_data scanned_aps[MAXIMUM_AP];  // Storage for discovered networks
extern uint16_t scanned_ap_count;  // Number of networks found
const char* auth_mode_type(wifi_auth_mode_t auth_mode);

void wifi_init_sta(wifi_sta_start_config_t *config);
void get_wifi_sta_state(wifi_sta_state_t *state);
void wifi_scan();
void wifi_stop_sta();
void get_wifi_ip(char *ip);
#endif // WIFI_MANAGER_HPP

