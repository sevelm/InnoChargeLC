#include "A_Task_OCPP.hpp"

#include <string.h>

#include "Arduino.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "ArduinoJson.h"
#include "WebSocketsClient.h"

namespace {

static const char* TAG = "Task_OCPP";

constexpr uint8_t OCPP_QUEUE_DEPTH = 16;
constexpr uint32_t OCPP_HEARTBEAT_MS = 60000;
constexpr uint32_t OCPP_RETRY_CONFIG_CHECK_MS = 3000;

QueueHandle_t s_eventQueue = nullptr;
SemaphoreHandle_t s_mutex = nullptr;

ocpp_server_config_t s_cfg{};
ocpp_runtime_stats_t s_stats{};

WebSocketsClient s_wsClient;
bool s_wsConfigured = false;
bool s_bootNotificationSent = false;
bool s_bootAccepted = false;

String s_lastMsgId;
uint32_t s_lastHeartbeatMs = 0;
uint32_t s_lastConfigRetryMs = 0;

struct ws_url_parts_t {
    bool valid;
    bool secure;
    String host;
    uint16_t port;
    String path;
};

static ws_url_parts_t parse_ws_url(const char* url) {
    ws_url_parts_t out{};
    out.valid = false;
    out.secure = false;
    out.port = 0;

    if (url == nullptr || url[0] == '\0') return out;

    String s(url);
    int schemePos = s.indexOf("://");
    if (schemePos <= 0) return out;

    String scheme = s.substring(0, schemePos);
    String rest = s.substring(schemePos + 3);
    if (scheme == "ws") {
        out.secure = false;
    } else if (scheme == "wss") {
        out.secure = true;
    } else {
        return out;
    }

    int slashPos = rest.indexOf('/');
    String hostPort = (slashPos >= 0) ? rest.substring(0, slashPos) : rest;
    out.path = (slashPos >= 0) ? rest.substring(slashPos) : "/";
    if (out.path.length() == 0) out.path = "/";

    int colonPos = hostPort.lastIndexOf(':');
    if (colonPos > 0) {
        out.host = hostPort.substring(0, colonPos);
        out.port = static_cast<uint16_t>(hostPort.substring(colonPos + 1).toInt());
    } else {
        out.host = hostPort;
        out.port = out.secure ? 443 : 80;
    }

    if (out.host.length() == 0 || out.port == 0) return out;

    out.valid = true;
    return out;
}

static void ws_send_text(const String& payload) {
    String out = payload;
    s_wsClient.sendTXT(out);
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        s_stats.sentFrames++;
        xSemaphoreGive(s_mutex);
    }
}

static void send_boot_notification() {
    // OCPP 1.6J Frame: [2, "<id>", "BootNotification", { ... }]
    JsonDocument doc;
    const uint32_t id = static_cast<uint32_t>(esp_random());
    s_lastMsgId = String(id, HEX);

    JsonArray root = doc.to<JsonArray>();
    root.add(2);
    root.add(s_lastMsgId);
    root.add("BootNotification");

    JsonObject payload = root.add<JsonObject>();
    payload["chargePointVendor"] = "InnoCharge";
    payload["chargePointModel"] = "InnoChargeLC";
    payload["chargePointSerialNumber"] = s_cfg.chargePointId;
    payload["firmwareVersion"] = "scaffold";

    String msg;
    serializeJson(doc, msg);
    ws_send_text(msg);

    s_bootNotificationSent = true;
}

static void send_heartbeat() {
    // OCPP 1.6J Frame: [2, "<id>", "Heartbeat", {}]
    JsonDocument doc;
    const uint32_t id = static_cast<uint32_t>(esp_random());
    const String msgId = String(id, HEX);

    JsonArray root = doc.to<JsonArray>();
    root.add(2);
    root.add(msgId);
    root.add("Heartbeat");
    root.add(JsonObject());

    String msg;
    serializeJson(doc, msg);
    ws_send_text(msg);
}

static void process_event(const ocpp_evse_event_t& ev) {
    // Platzhalter fuer spaeteres Mapping EVSE -> OCPP:
    // - CP-Status -> StatusNotification
    // - RFID -> Authorize / StartTransaction
    // - Meter -> MeterValues
    // Derzeit nur Logging, um Taskfluss sichtbar zu halten.
    ESP_LOGI(TAG, "Event type=%u value=%ld ts=%lu",
             static_cast<unsigned>(ev.type),
             static_cast<long>(ev.value),
             static_cast<unsigned long>(ev.tsMs));
}

static void ws_event_handler(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            ESP_LOGW(TAG, "WebSocket disconnected");
            s_wsConfigured = false;
            s_bootNotificationSent = false;
            s_bootAccepted = false;
            if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                s_stats.wsConnected = false;
                s_stats.bootAccepted = false;
                xSemaphoreGive(s_mutex);
            }
            break;
        case WStype_CONNECTED:
            ESP_LOGI(TAG, "WebSocket connected");
            if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                s_stats.wsConnected = true;
                xSemaphoreGive(s_mutex);
            }
            send_boot_notification();
            s_lastHeartbeatMs = millis();
            break;
        case WStype_TEXT: {
            if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                s_stats.recvFrames++;
                xSemaphoreGive(s_mutex);
            }

            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, payload, length);
            if (err) {
                ESP_LOGW(TAG, "Invalid OCPP JSON (%s)", err.c_str());
                return;
            }

            // Minimal: CALLRESULT fuer BootNotification erkennen.
            // Format: [3, "<id>", {status:"Accepted", ...}]
            JsonArray root = doc.as<JsonArray>();
            if (root.size() >= 3 && root[0].is<int>() && root[0].as<int>() == 3) {
                const char* msgId = root[1].as<const char*>();
                if (msgId != nullptr && s_lastMsgId == msgId) {
                    JsonObject result = root[2].as<JsonObject>();
                    const char* status = result["status"] | "";
                    if (strcmp(status, "Accepted") == 0) {
                        s_bootAccepted = true;
                        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                            s_stats.bootAccepted = true;
                            xSemaphoreGive(s_mutex);
                        }
                        ESP_LOGI(TAG, "BootNotification accepted by CSMS");
                    } else {
                        ESP_LOGW(TAG, "BootNotification status: %s", status);
                    }
                }
            }
            break;
        }
        default:
            break;
    }
}

static void try_configure_websocket() {
    ws_url_parts_t p = parse_ws_url(s_cfg.wsUrl);
    if (!p.valid) return;

    if (p.secure) {
        // Fuer "wss://" folgt spaeter Zertifikats-/TLS-Handling.
        ESP_LOGW(TAG, "wss:// not active in scaffold yet. Use ws:// for now.");
        return;
    }

    ESP_LOGI(TAG, "Connecting WS host=%s port=%u path=%s",
             p.host.c_str(), p.port, p.path.c_str());
    s_wsClient.begin(p.host.c_str(), p.port, p.path.c_str());
    s_wsClient.onEvent(ws_event_handler);
    s_wsClient.setReconnectInterval(5000);
    s_wsConfigured = true;
}

} // namespace

void ocpp_set_server_config(const ocpp_server_config_t* cfg) {
    if (cfg == nullptr || s_mutex == nullptr) return;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        memcpy(&s_cfg, cfg, sizeof(s_cfg));
        xSemaphoreGive(s_mutex);
    }
}

bool ocpp_enqueue_event(const ocpp_evse_event_t& ev, TickType_t timeoutTicks) {
    if (s_eventQueue == nullptr) return false;
    return xQueueSend(s_eventQueue, &ev, timeoutTicks) == pdPASS;
}

bool ocpp_get_runtime_stats(ocpp_runtime_stats_t* outStats) {
    if (outStats == nullptr || s_mutex == nullptr) return false;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        *outStats = s_stats;
        xSemaphoreGive(s_mutex);
        return true;
    }
    return false;
}

void A_Task_OCPP(void* /*pvParameter*/) {
    s_eventQueue = xQueueCreate(OCPP_QUEUE_DEPTH, sizeof(ocpp_evse_event_t));
    s_mutex = xSemaphoreCreateMutex();

    if (s_eventQueue == nullptr || s_mutex == nullptr) {
        ESP_LOGE(TAG, "Init failed (queue/mutex)");
        vTaskDelete(nullptr);
        return;
    }

    memset(&s_cfg, 0, sizeof(s_cfg));
    memset(&s_stats, 0, sizeof(s_stats));

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_stats.taskInitialized = true;
        xSemaphoreGive(s_mutex);
    }

    ESP_LOGI(TAG, "OCPP task started (scaffold)");

    while (true) {
        s_wsClient.loop();

        // Konfiguration zyklisch pruefen (falls sie spaeter gesetzt wird).
        const uint32_t now = millis();
        if (!s_wsConfigured && (now - s_lastConfigRetryMs) >= OCPP_RETRY_CONFIG_CHECK_MS) {
            s_lastConfigRetryMs = now;
            try_configure_websocket();
        }

        // Heartbeat erst nach akzeptierter BootNotification.
        if (s_bootAccepted && (now - s_lastHeartbeatMs) >= OCPP_HEARTBEAT_MS) {
            send_heartbeat();
            s_lastHeartbeatMs = now;
        }

        ocpp_evse_event_t ev{};
        if (xQueueReceive(s_eventQueue, &ev, pdMS_TO_TICKS(25)) == pdPASS) {
            process_event(ev);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
