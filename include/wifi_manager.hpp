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

void wifi_init_sta(wifi_sta_start_config_t *config);
void get_wifi_sta_state(wifi_sta_state_t *state);
void wifi_scan();
void wifi_stop_sta();

