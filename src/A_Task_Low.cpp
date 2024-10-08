#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "A_Task_Low.hpp"
#include "A_Task_CP.hpp"
#include "SPIFFS.h"


#include "AA_globals.h"
#include "esp_netif.h"
#include "ethernet_manager.hpp"


const char *CP_LOGI = "Task_Low: ";


bool enableWifiScan = false;
bool prevWifiEnable; 


//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
void A_Task_Low(void *pvParameter){
 char eth_ip[16];

//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
    while(1){
        get_eth_ip(eth_ip);
        ESP_LOGI(CP_LOGI, "IP: %s", eth_ip);
 // Zyklischer WiFi-Scan
 
     if (preferences.getBool("wifiEnable", false) && !prevWifiEnable){
        enableWifiScan = true;
        prevWifiEnable = true;
     }
 
 
 if (enableWifiScan)
 {
        ESP_LOGI(CP_LOGI, "Performing WiFi scan");
        wifi_scan();
        enableWifiScan = false;
    }
    //    ESP_LOGI(CP_LOGI, "highVoltage: %f", highVoltage);
    //    ESP_LOGI(CP_LOGI, "CP: %s", cpStateToName(currentCpState));

        vTaskDelay(2000/portTICK_PERIOD_MS); // verzögere den Task um sekunden

    }
}