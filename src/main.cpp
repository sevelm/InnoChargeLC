//main.cpp

#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "SPIFFS.h"
#include "ESPAsyncWebServer.h" // needed to create a simple webserver (make sure tools -> board is set to ESP32, otherwise you will get a "WebServer.h: No such file or directory" error)
#include "AsyncTCP.h"
#include "WebSocketsServer.h"  // needed for instant communication between client and server through Websockets
#include "ArduinoJson.h"       // needed for JSON encapsulation (send multiple variables with one string)
#include <Preferences.h>

#include "ethernet_manager.hpp"
#include "lock_ctrl.hpp"
#include "A_Task_Low.hpp"
#include "A_Task_Web.hpp"
#include "ledEffect.hpp"

#include "A_Task_CP.hpp"
#include "A_Task_MB.hpp"
#include "wifi_manager.hpp"

#include "esp_wifi.h" 
#include "AA_globals.h"
#include <mDNS.h>

const char *MAIN_TAG = "Web: ";
// Globals
Preferences preferences;

// cp_measurements_t measurements = {0.0, 0.0, {0.0}, 0};
NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(8, 10);
int cpState;
float highVoltage;
charging_state_t currentCpState = StateA_NotConnected;

// Globale Variablen für WiFi-Status
bool wifiEnabled = false; // Muss initialisiert werden
wifi_sta_start_config_t wifi_sta_config = {
    .retry_interval = 5000,
    .max_retry = -1
};


void start_mdns_service()
{
    //initialize mDNS service
    esp_err_t err = mdns_init();
    if (err) {
        ESP_LOGI(MAIN_TAG, "MDNS Init failed: %d\n", err);
        return;
    }

    //set hostname
    mdns_hostname_set("my-esp32");
    //set default instance
    mdns_instance_name_set("Jhon's ESP32 Thing");
    ESP_LOGI(MAIN_TAG, "MDNS create");
}



//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////

// ###################### HTTP Event Handler

void setup() {
    delay(5000); // 1 Sekunde warten
    ESP_LOGI(MAIN_TAG, "Starting up EVSE Test Programm!");

    //######################### Preferences
    preferences.begin("store", false);                      //create folder

    // ######################### Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // ######################### Initialize TCP/IP Stack and Default Event Loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(MAIN_TAG, "Starting Ethernet...");
    restart_new_settings_eth();
    ESP_LOGI(MAIN_TAG, "Ethernet started.");

    // ######################### LED-Pixles
    strip.Begin(); // Initialize NeoPixel strip object (REQUIRED)
    strip.Show(); // Initialize all strip to 'off'
    for (int i = 0; i < 8; i++) {
        strip.SetPixelColor(i, RgbColor(0, 0, 255));
    }
    strip.Show(); // Send the updated pixel colors to the hardware.

    // ######################### Webserver Start
    // Initialize SPIFFS
    if (!SPIFFS.begin(true)) {
        ESP_LOGI(MAIN_TAG, "An Error has occurred while mounting SPIFFS");
        return;
    }
    ESP_LOGI(MAIN_TAG, "SPIFFS mounted successfully");
       
    
    //xTaskCreate(demo_monitoring_task, "Demo Monitoring Task", 4096, NULL, 1, NULL);

    // connect to wifi
    char ssid[32] = {0}; 
    char pwd[64] = {0}; 
    size_t ssid_size = preferences.getBytes("wifi_ssid", ssid, sizeof(ssid));
    size_t pwd_size = preferences.getBytes("wifi_pwd", pwd, sizeof(pwd));
    strcpy(wifi_sta_config.ssid, ssid);
    strcpy(wifi_sta_config.passphrase, pwd);

    // Schalter zum aktivieren und deaktivieren
    wifiEnabled = preferences.getBool("wifiEnable", false);
    if (wifiEnabled) {
        wifi_init_sta(&wifi_sta_config);
    } else {
        preferences.putBool("wifiStatic", false);
    }

    // generate hostname
   // if (!MDNS.begin("esp32")) {  // "esp32" wird der Hostname
  //      ESP_LOGI(MAIN_TAG, "ERROR mDNS");
  //      return;
  //  }
  //  ESP_LOGI(MAIN_TAG, "mDNS started. Access at http://esp32.local");
    // HTTP-Dienst hinzufügen
//    MDNS.addService("http", "tcp", 80);


start_mdns_service();




// ######################### Create Task and Start
    xTaskCreatePinnedToCore(A_Task_CP, "Controlpilot Task", 8192, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(A_Task_MB, "Task_Modbus_Operation", 8192, NULL, 2, NULL, 1);   
    xTaskCreatePinnedToCore(A_Task_Web, "Task_Web_Operation", 8192, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(A_Task_Low, "Task_Low_Operation", 8192, NULL, 5, NULL, 1);


    // Scan WiFi networks
   // wifi_scan();

    // xTaskCreate(pp_monitoring_task, "PP Monitoring Task", 4096, NULL, 1, NULL);
    // xTaskCreate(lock_monitor_task, "Lock Monitor Task", 2048, NULL, 5, NULL);
    // xTaskCreate(relay_ctrl_test_task, "Relay Control Test Task", 2048, NULL, 5, NULL);
}

//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
void loop() {
    // control LED
    callLedEffect();

// WiFi control based on wifiEnabled
//bool newWifiEnabled = preferences.getBool("wifiEnable", false);
//if (newWifiEnabled != wifiEnabled) {
 //   wifiEnabled = newWifiEnabled;
 //   if (wifiEnabled) {
 //       ESP_LOGI(MAIN_TAG, "WiFi is being activated");
 //       wifi_init_sta(&wifi_sta_config);
 //   } else {
 //       ESP_LOGI(MAIN_TAG, "WiFi is being deactivated");

  //  }
//}


    // lock_lock();
    // vTaskDelay(5000 / portTICK_PERIOD_MS);
    // release_lock();
    // vTaskDelay(5000 / portTICK_PERIOD_MS);
}


