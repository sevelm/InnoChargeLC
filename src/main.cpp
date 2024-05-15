#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "relay_ctrl.hpp"
#include "ethernet_manager.hpp"
#include "control_pilot.hpp"
#include "lock_ctrl.hpp"
#include "A_TaskLow.hpp"
#include "ledEffect.hpp"
#include "AA_globals.h"

const char *MAIN_TAG = "Main: ";


//Globals

cp_measurements_t measurements = {0.0, 0.0, {0.0}, 0};
NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(8, 10);
int cpState;


//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
void setup() {

  ESP_LOGI(MAIN_TAG, "Starting up EVSE Test Programm!");


//######################### LED-Pixles 
  strip.Begin();              // INITIALIZE NeoPixel strip object (REQUIRED)
  strip.Show();               // Initialize all strip to 'off'
    for (int i = 0; i < 8; i++){
       strip.SetPixelColor(i, RgbColor(0, 0, 255)); 
    }
  strip.Show();   // Send the updated pixel colors to the hardware.



//######################### Create Task and Start
  // xTaskCreate(control_pilot_task, "Control Pilot Task", 4096, NULL, 5, NULL);
  // xTaskCreate(lock_monitor_task, "Lock Monitor Task", 2048, NULL, 5, NULL);
  // xTaskCreate(A_TaskLow, "Task_Low_Operation", 4096, NULL, 5, NULL);

  // xTaskCreate(relay_ctrl_test_task, "Relay Control Test Task", 2048, NULL, 5, NULL);

 // esp_netif_init();
//  esp_event_loop_create_default();
//  start_eth();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(MAIN_TAG, "Starting Ethernet...");
    start_eth(); // Start Ethernet
    ESP_LOGI(MAIN_TAG, "Ethernet started.");


  }


//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
void loop() {
// control LED
callLedEffect();


//  lock_lock();
//  vTaskDelay(5000 / portTICK_PERIOD_MS);
//  release_lock();
//  vTaskDelay(5000 / portTICK_PERIOD_MS);




}
