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




// * =====================================================
// * OTA-UPLOAD-HANDLER
// * -----------------------------------------------------


/* ---------- Upload‑Handler MAIN ---------- */
void setupUploadMain()
{
    server.on("/uploadfw", HTTP_POST,

    /* ---------- (1) Antwort nach Abschluss ---------- */
    [](AsyncWebServerRequest *req)
    {
        bool err = Update.hasError();
        req->send(err ? 500 : 200, "text/plain",
                  err ? "flash error" : "flash ok");

        otaMain.code    = err ? -1 : 1;
        otaMain.message = err ? "flash error" : "flash ok";

        String json;
        JsonDocument doc;
        doc["otaMainProgress"] = otaMain.progress;
        doc["otaMainCode"]     = otaMain.code;
        doc["otaMainMessage"]  = otaMain.message;
        serializeJson(doc, json);

        for (auto &c : subscribedClients)
            if (c.second == "system")
                webSocket.sendTXT(c.first, json);

        if (!err) {                        // bei Erfolg neu starten
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        }
    },

    /* ---------- (2) Upload‑Stream ------------------- */
    [](AsyncWebServerRequest *req, String, size_t idx,
       uint8_t *data, size_t len, bool final)
    {
        static bool   mainFailed = false;
        static size_t totalLen   = 0;      // Gesamtgröße der BIN

        if (idx == 0) {                    // erster Block
            totalLen = req->contentLength();

            /* Progress‑Callback erst jetzt registrieren */
            Update.onProgress([&](size_t done, size_t /*unused*/){
                otaMain.progress = totalLen ? (done * 100 / totalLen) : 0;
                otaMain.message  = "progress";
                ESP_LOGI("OTA Main","progress %u %%", otaMain.progress);
            });

            if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
                mainFailed   = true;
                otaMain.code = -2;
                ESP_LOGE("OTA Main","begin() %s", Update.errorString());
                return;
            }
        }

        if (!mainFailed && Update.write(data, len) != len) {
            mainFailed   = true;
            otaMain.code = -3;
            ESP_LOGE("OTA Main","write() %s", Update.errorString());
        }

        if (final) {
            Update.onProgress(nullptr);    // Callback lösen

            if (!mainFailed && !Update.end(true)) {
                otaMain.code = -4;
                ESP_LOGE("OTA Main","end() %s", Update.errorString());
            } else if (!mainFailed) {
                otaMain.code = 1;
                ESP_LOGI("OTA Main","firmware upload finished OK");
            }
        }
    });
}

/* ---------- Upload‑Handler SPIFFS ---------- */
void setupUploadUi()
{
    server.on("/uploadui", HTTP_POST,

    /* ---------- (1) Antwort nach Abschluss ---------- */
    [](AsyncWebServerRequest *req)
    {
        bool err = Update.hasError();
        req->send(err ? 500 : 200, "text/plain",
                  err ? "fs flash error" : "fs flash ok");

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

        if (!err) {                        // bei Erfolg neu starten
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        }
    },

    /* ---------- (2) Upload‑Stream ------------------- */
    [](AsyncWebServerRequest *req, String, size_t idx,
       uint8_t *data, size_t len, bool final)
    {
        static bool   uiFailed  = false;
        static size_t totalLen  = 0;       // Gesamtgröße des SPIFFS‑Images

        if (idx == 0) {                    // erster Block
            if (SPIFFS.begin()) SPIFFS.end();          // unmount
            totalLen = req->contentLength();

            /* Progress‑Callback erst jetzt registrieren */
            Update.onProgress([&](size_t done, size_t /*unused*/){
                otaUi.progress = totalLen ? (done * 100 / totalLen) : 0;
                otaUi.message  = "progress";
                ESP_LOGI("OTA Ui","progress %u %%", otaUi.progress);
            });

            if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
                uiFailed   = true;
                otaUi.code = -2;
                ESP_LOGE("OTA Ui","begin() %s", Update.errorString());
                return;
            }
        }

        if (!uiFailed && Update.write(data, len) != len) {
            uiFailed   = true;
            otaUi.code = -3;
            ESP_LOGE("OTA Ui","write() %s", Update.errorString());
        }

        if (final) {
            Update.onProgress(nullptr);    // Callback lösen

            if (!uiFailed && !Update.end(true)) {
                otaUi.code = -4;
                ESP_LOGE("OTA Ui","end() %s", Update.errorString());
            } else if (!uiFailed) {
                otaUi.code = 1;
                ESP_LOGI("OTA Ui","SPIFFS upload finished OK");
            }
        }
    });
}