// web_ota.cpp
#include "A_Task_Web.hpp"                 // nur dieser Header
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <Update.h>
#include "ESPAsyncWebServer.h"
#include "WebSocketsServer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG_OTA_MAIN = "OTA Main";
static const char* TAG_OTA_UI   = "OTA Ui";

void setupUploadMain() {
  server.on("/uploadfw", HTTP_POST,
    /* (1) Antwort nach Abschluss */
    [](AsyncWebServerRequest *req) {
      const bool err = Update.hasError();
      req->send(err ? 500 : 200, "text/plain", err ? "flash error" : "flash ok");

      otaMain.code    = err ? -1 : 1;
      otaMain.message = err ? "flash error" : "flash ok";

      // Status an "system"-Abonnenten pushen
      String json;
      JsonDocument doc;
      doc["otaMainProgress"] = otaMain.progress;
      doc["otaMainCode"]     = otaMain.code;
      doc["otaMainMessage"]  = otaMain.message;
      serializeJson(doc, json);

      for (auto &c : subscribedClients)
        if (c.second == "system")
          webSocket.sendTXT(c.first, json);

      if (!err) {
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
      }
    },
    /* (2) Upload-Stream */
    [](AsyncWebServerRequest * /*req*/, String, size_t idx,
       uint8_t *data, size_t len, bool final)
    {
      static bool   failed   = false;
      static size_t totalLen = 0;

      if (idx == 0) {                         // erster Block
        failed   = false;
        totalLen = Update.size();             // optional
        Update.onProgress([&](size_t done, size_t){
          otaMain.progress = (totalLen ? (done * 100 / totalLen) : 0);
          otaMain.message  = "progress";
          ESP_LOGI(TAG_OTA_MAIN, "progress %u %%", otaMain.progress);
        });
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
          failed      = true;
          otaMain.code = -2;
          ESP_LOGE(TAG_OTA_MAIN, "begin() %s", Update.errorString());
          return;
        }
      }

      if (!failed && Update.write(data, len) != len) {
        failed      = true;
        otaMain.code = -3;
        ESP_LOGE(TAG_OTA_MAIN, "write() %s", Update.errorString());
      }

      if (final) {
        Update.onProgress(nullptr);
        if (!failed && !Update.end(true)) {
          otaMain.code = -4;
          ESP_LOGE(TAG_OTA_MAIN, "end() %s", Update.errorString());
        } else if (!failed) {
          otaMain.code = 1;
          ESP_LOGI(TAG_OTA_MAIN, "firmware upload finished OK");
        }
      }
    }
  );
}

void setupUploadUi() {
  server.on("/uploadui", HTTP_POST,
    /* (1) Antwort nach Abschluss */
    [](AsyncWebServerRequest *req) {
      const bool err = Update.hasError();
      req->send(err ? 500 : 200, "text/plain", err ? "fs flash error" : "fs flash ok");

      otaUi.code    = err ? -1 : 1;
      otaUi.message = err ? "fs flash error" : "fs flash ok";

      String json;
      JsonDocument doc;
      doc["otaUiProgress"] = otaUi.progress;
      doc["otaUiCode"]     = otaUi.code;
      doc["otaUiMessage"]  = otaUi.message;
      serializeJson(doc, json);

      for (auto &c : subscribedClients)
        if (c.second == "system")
          webSocket.sendTXT(c.first, json);

      if (!err) {
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
      }
    },
    /* (2) Upload-Stream */
    [](AsyncWebServerRequest * /*req*/, String, size_t idx,
       uint8_t *data, size_t len, bool final)
    {
      static bool   failed   = false;
      static size_t totalLen = 0;

      if (idx == 0) {                         // erster Block
        failed   = false;
        if (SPIFFS.begin()) SPIFFS.end();     // unmount vor U_SPIFFS
        totalLen = Update.size();             // optional
        Update.onProgress([&](size_t done, size_t){
          otaUi.progress = (totalLen ? (done * 100 / totalLen) : 0);
          otaUi.message  = "progress";
          ESP_LOGI(TAG_OTA_UI, "progress %u %%", otaUi.progress);
        });
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
          failed      = true;
          otaUi.code   = -2;
          ESP_LOGE(TAG_OTA_UI, "begin() %s", Update.errorString());
          return;
        }
      }

      if (!failed && Update.write(data, len) != len) {
        failed     = true;
        otaUi.code  = -3;
        ESP_LOGE(TAG_OTA_UI, "write() %s", Update.errorString());
      }

      if (final) {
        Update.onProgress(nullptr);
        if (!failed && !Update.end(true)) {
          otaUi.code = -4;
          ESP_LOGE(TAG_OTA_UI, "end() %s", Update.errorString());
        } else if (!failed) {
          otaUi.code = 1;
          ESP_LOGI(TAG_OTA_UI, "SPIFFS upload finished OK");
        }
      }
    }
  );
}
