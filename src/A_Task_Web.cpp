#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "A_Task_Web.hpp"
#include "SPIFFS.h"
#include "ESPAsyncWebServer.h"
#include "AsyncTCP.h"
#include "WebSocketsServer.h"
#include "ArduinoJson.h"

#include "AA_globals.h"
#include "A_Task_CP.hpp"
#include "control_pilot.hpp"


WebSocketsServer webSocket = WebSocketsServer(81);

const char *CP_LO = "Task_Web: ";
volatile bool clientConnected = false;

void webSocketEvent(byte num, WStype_t type, uint8_t * payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            ESP_LOGI(CP_LO, "Client %s disconnected", String(num).c_str());
            clientConnected = false;
            break;
        case WStype_CONNECTED:
            ESP_LOGI(CP_LO, "Client %s connected", String(num).c_str());
            clientConnected = true;
            break;
        case WStype_TEXT:
            ESP_LOGI(CP_LO, "Raw data received: %.*s", length, (char*)payload);
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);
            if (error) {
                ESP_LOGI(CP_LO, "deserializeJson() failed: %s", error.f_str());
                return;
            } else {
                JsonVariantConst val;
                val = doc["setChargeCurrent"];              if (val != nullptr) { set_charging_current(val); }
                val = doc["setCpRelay"];                    if (val == true) { turn_on_cp_relay(); }
                val = doc["setCpRelay"];                    if (val == false) { turn_off_cp_relay(); }
                serializeJsonPretty(doc, Serial);
            }
            break;
    }
}

void webSocketCreate(void *pvParameter) {
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
    while(1) {
        if (clientConnected) {
            String jsonString = "";
            JsonDocument doc;
            JsonObject object = doc.to<JsonObject>();
            object["cpState"] = cpStateToName(currentCpState);
            object["cpRelayState"] = get_cp_relays_status();           
            serializeJson(doc, jsonString);
          //  ESP_LOGI(CP_LO, "%s", jsonString.c_str());
            webSocket.broadcastTXT(jsonString);
        }
        vTaskDelay(800 / portTICK_PERIOD_MS);
    }
}

void A_Task_Web(void *pvParameter) {
//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

    xTaskCreate(webSocketCreate, "WebSocketCreateTask", 2048, NULL, 1, NULL);
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
    while(1) {
        webSocket.loop();
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}
