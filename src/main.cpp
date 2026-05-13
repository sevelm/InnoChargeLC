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
#include "rfid_db.hpp"
#include "time_service.hpp"
#include "charge_session_log.hpp"
#include "session_mailer.hpp"
#include "dynamic_power_limit.hpp"

#include "esp_wifi.h" 
#include "AA_globals.h"

#include "esp_ota_ops.h"
#include "esp_partition.h"

const char *MAIN_TAG = "Web: ";
// Globals
Preferences preferences;

// cp_measurements_t measurements = {0.0, 0.0, {0.0}, 0};
NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(8, 10); // Set to Pin 10
int cpState;
float highVoltage, getCpDuty, setCpDuty;
volatile int ledDummyState = 1;

charging_status_t currentCpState{ StateA_NotConnected, false, false };

// Globale Variablen für WiFi-Status
bool wifiEnabled = false; // Muss initialisiert werden
wifi_sta_start_config_t wifi_sta_config = {
    .retry_interval = 5000,
    .max_retry = -1
};

// Dipswitch 2 (rechts) IP-Fallback 10.10.10.10
bool rescueMode = false;

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

    // ######################### Default values for fresh NVS
    if (!preferences.isKey("wallboxName")) preferences.putString("wallboxName", "InnoCharge");
    if (!preferences.isKey("mailServer")) preferences.putString("mailServer", "");
    if (!preferences.isKey("mailUser")) preferences.putString("mailUser", "");
    if (!preferences.isKey("mailPass")) preferences.putString("mailPass", "");
    if (!preferences.isKey("mailFrom")) preferences.putString("mailFrom", "");
    if (!preferences.isKey("mailTo")) preferences.putString("mailTo", "");
    if (!preferences.isKey("mailSubject")) preferences.putString("mailSubject", "InnoCharge charge sessions");
    if (!preferences.isKey("dpl0cfg")) preferences.putString("dpl0cfg", "");
    if (!preferences.isKey("dpl1cfg")) preferences.putString("dpl1cfg", "");
    if (!preferences.isKey("dpl2cfg")) preferences.putString("dpl2cfg", "");
    if (!preferences.isKey("rfidUsers")) preferences.putString("rfidUsers", "");

    // ######################### Initialize TCP/IP Stack and Default Event Loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    pinMode(DIP_SWITCH_1, INPUT_PULLUP);
    pinMode(DIP_SWITCH_2, INPUT_PULLUP);
    rescueMode = (digitalRead(DIP_SWITCH_2) == LOW);   // DIP 2 ON?
    if (rescueMode) {
        ethernet_start_config_t rescueCfg = {
            .ip_addr = {10, 10, 10, 10},
            .netmask = {255, 255, 255, 0},
            .gw      = {10, 10, 10, 1},     // Gateway beliebig, sonst 0,0,0,0
            .dns1    = {10, 10, 10, 1},     // DNS  optional
            .dns2    = {8,  8,  8,  8}
        };
        ESP_LOGW(MAIN_TAG, "Rescue‑IP activated → 10.10.10.10");
        start_eth(false, &rescueCfg);        // DHCP OFF
    } else {
        ESP_LOGI(MAIN_TAG, "Starting Ethernet...");
        restart_new_settings_eth();
        ESP_LOGI(MAIN_TAG, "Ethernet started.");
    }



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
    // ---------- Größe ausgeben ----------
    size_t total = SPIFFS.totalBytes();   // Gesamt­größe der Partition
    size_t used  = SPIFFS.usedBytes();    // Belegter Speicher

    ESP_LOGI(MAIN_TAG,
            "SPIFFS size: %u bytes total, %u bytes used (%.1f %%)",
            total, used,
            total ? (used * 100.0 / total) : 0.0);


    rfid_db_begin();
    time_service_begin();
    charge_session_log_begin();
    session_mailer_begin();
    dynamic_power_limit_begin();

    // connect to wifi
    char ssid[32] = {0}; 
    char pwd[64] = {0}; 
    size_t ssid_size = preferences.getBytes("wifi_ssid", ssid, sizeof(ssid));
    size_t pwd_size = preferences.getBytes("wifi_pwd", pwd, sizeof(pwd));
    strcpy(wifi_sta_config.ssid, ssid);
    strcpy(wifi_sta_config.passphrase, pwd);

    // Schalter zum aktivieren und deaktivieren
    wifiEnabled = preferences.getBool("wifiEnable", false);
    if (rescueMode) {
        wifi_start_rescue_ap();
    } else if (wifiEnabled) {
        wifi_init_sta(&wifi_sta_config);
    } else {
        preferences.putBool("wifiStatic", false);
    }
    // RFID or Energymeter Enable
    sdm.enable  = preferences.getBool("emEnable",  false);
    sdm.invSign = preferences.getBool("emSignEnable", false);
    sdm.type    = (energy_meter_type_t)preferences.getUChar("emType", EnergyMeter_EastronSdm630);
    sdm.modbusId = preferences.getUChar("emMbId", 1);
    if (sdm.modbusId < 1 || sdm.modbusId > 247) sdm.modbusId = 1;
    rfid.enable = preferences.getBool("rfidEnable", false);
    rfid.modbusId = preferences.getUChar("rfidMbId", 2);
    if (rfid.modbusId < 1 || rfid.modbusId > 247) rfid.modbusId = 2;
    rfid.buzzer = preferences.getBool("rfidBuzzer", false);
    rfid.led = preferences.getUChar("rfidLed", 0);
    if (rfid.led > 2) rfid.led = 0;
    rfidAuth.required = preferences.getBool("rfidAuthReq", false);
    rfidAuth.authorized = false;
    chargeAuthSession.authorized = false;
    chargeAuthSession.vehicleWasConnected = false;
    chargeAuthSession.authorizationGrantedMillis = 0;
    chargeAuthSession.lastChargeActiveMillis = 0;
    chargeAuthSession.authorizationGrantedTime = 0;
    chargeAuthSession.lastChargeActiveTime = 0;
    chargeAuthSession.idTag = "";
    chargeAuthSession.userName = "";
    chargeAuthSession.maxChargeMinutes = 0;

// ######################### Create Task and Start

    xTaskCreatePinnedToCore(A_Task_CP, "Controlpilot Task", 8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(A_Task_MB, "Task_Modbus_Operation", 8192, NULL, 10, NULL, 0);   
    xTaskCreatePinnedToCore(A_Task_Web, "Task_Web_Operation", 8192, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(A_Task_Low, "Task_Low_Operation", 8192, NULL, 1, NULL, 1);

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



