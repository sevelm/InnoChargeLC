#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"

#include "relay_ctrl.hpp"
#include "ethernet_manager.hpp"
#include "control_pilot.hpp"

const char *MAIN_TAG = "Main: ";

void setup() {
  ESP_LOGI(MAIN_TAG, "Starting up EVSE Test Programm!");
  // xTaskCreate(relay_ctrl_test_task, "Relay Control Test Task", 2048, NULL, 5, NULL);
  xTaskCreate(control_pilot_task, "Control Pilot Task", 4096, NULL, 5, NULL);
  esp_netif_init();
  esp_event_loop_create_default();
  start_eth();
  }  
void loop() {
  ESP_LOGI(MAIN_TAG, "Hello world!  Loop");
  vTaskDelay(5000 / portTICK_PERIOD_MS);
}
