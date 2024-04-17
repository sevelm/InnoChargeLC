#include "relay_ctrl.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

const char *RELAY_TAG = "Relay Control: ";
#define RELAY_PIN 36

void init_relay_ctrl() {
    ESP_LOGV(RELAY_TAG, "Initializing relay control");
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);
    ESP_LOGV(RELAY_TAG, "Relay control initialized");
}

void turn_relay_on() {
    ESP_LOGV(RELAY_TAG, "Turning relay on");
    digitalWrite(RELAY_PIN, HIGH);
    ESP_LOGV(RELAY_TAG, "Relay turned on");
}

void turn_relay_off() {
    ESP_LOGV(RELAY_TAG, "Turning relay off");
    digitalWrite(RELAY_PIN, LOW);
    ESP_LOGV(RELAY_TAG, "Relay turned off");
}

void relay_ctrl_test_task(void *pvParameter) {
    ESP_LOGI(RELAY_TAG, "Starting relay control test task");
    init_relay_ctrl();
    while (true) {
        ESP_LOGI(RELAY_TAG, "Turning relay on for 30 seconds");
        turn_relay_on();
        vTaskDelay(10000 / portTICK_PERIOD_MS);
        ESP_LOGI(RELAY_TAG, "Turning relay off for 30 seconds");
        turn_relay_off();
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}