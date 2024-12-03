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
#include "wifi_manager.hpp"

const char *CP_LOGI = "Task_Low: ";


//bool enableWifiScan = false;
//bool prevWifiEnable; 


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
        ESP_LOGI(CP_LOGI, "ETH IP : %s", eth_ip);
        get_wifi_ip(eth_ip);
        ESP_LOGI(CP_LOGI, "WIFI IP: %s", eth_ip);

        //uint8_t ip[4] = {0}, netmask[4] = {0}, gw[4] = {0}, dns1[4] = {0}, dns2[4] = {0};
        //preferences.getBytes("wifi_dns1", dns1, sizeof(dns1));
        //ESP_LOGI(CP_LOGI, "DNS1: %d.%d.%d.%d", dns1[0], dns1[1], dns1[2], dns1[3]);



     //if (preferences.getBool("wifiEnable", false) && !prevWifiEnable){
     //   enableWifiScan = true;
     //   prevWifiEnable = true;
     //}
 
      //  wifi_sta_state_t wifi_status;
      //  get_wifi_sta_state(&wifi_status);
      //  printf("\n\n\n__________________________________________________________________");
      //  ESP_LOGI(CP_LOGI, "Wifi Connection Status: %d", wifi_status.connected);
      //  ESP_LOGI(CP_LOGI, "Wifi SSID: %s", wifi_status.ssid);
      //  ESP_LOGI(CP_LOGI, "Wifi IP Address: %d.%d.%d.%d", wifi_status.ip[0], wifi_status.ip[1], wifi_status.ip[2], wifi_status.ip[3]);
      //  ESP_LOGI(CP_LOGI, "Wifi Netmask: %d.%d.%d.%d", wifi_status.netmask[0], wifi_status.netmask[1], wifi_status.netmask[2], wifi_status.netmask[3]);
      //  ESP_LOGI(CP_LOGI, "Wifi Gateway: %d.%d.%d.%d", wifi_status.gateway[0], wifi_status.gateway[1], wifi_status.gateway[2], wifi_status.gateway[3]);
      //  ESP_LOGI(CP_LOGI, "Wifi DNS1: %d.%d.%d.%d", wifi_status.dns1[0], wifi_status.dns1[1], wifi_status.dns1[2], wifi_status.dns1[3]);
      //  ESP_LOGI(CP_LOGI, "Wifi DNS2: %d.%d.%d.%d", wifi_status.dns2[0], wifi_status.dns2[1], wifi_status.dns2[2], wifi_status.dns2[3]);
      //  ESP_LOGI(CP_LOGI, "Wifi MAC Address: %02x:%02x:%02x:%02x:%02x:%02x", wifi_status.mac[0], wifi_status.mac[1], wifi_status.mac[2], wifi_status.mac[3], wifi_status.mac[4], wifi_status.mac[5]);
      //  printf("__________________________________________________________________\n\n\n");
          


 //if (enableWifiScan)
 //{
   //     ESP_LOGI(CP_LOGI, "Performing WiFi scan");

     //   enableWifiScan = false;
   // }
    //    ESP_LOGI(CP_LOGI, "highVoltage: %f", highVoltage);
    //    ESP_LOGI(CP_LOGI, "CP: %s", cpStateToName(currentCpState));

        vTaskDelay(3000/portTICK_PERIOD_MS); // verzögere den Task um sekunden

    }
}