// A_Task_web.cpp

#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <vector>


#include "esp_log.h"
#include "esp_event.h"
#include "A_Task_Web.hpp"
#include "SPIFFS.h"
#include "ESPAsyncWebServer.h"
#include "AsyncTCP.h"
#include "WebSocketsServer.h"
#include "ArduinoJson.h"
#include <Update.h>
#include "esp_ota_ops.h"
#include "AA_globals.h"
#include "A_Task_CP.hpp"
#include "control_pilot.hpp"
#include "ledEffect.hpp"
#include "ethernet_manager.hpp"
#include "wifi_manager.hpp"
#include "rfid_db.hpp"
#include "time_service.hpp"
#include "charge_session_log.hpp"
#include "session_mailer.hpp"
#include "dynamic_power_limit.hpp"
#include "grid_protection.hpp"
#include "cp_diagnostic_log.hpp"
#include "esp_wifi.h"

#include "esp_timer.h"
#include <time.h>
#include <map>
#include <set>

#include <esp_ota_ops.h>        // ↯ einmal ganz oben in A_Task_Web.cpp


#ifndef FW_VERSION_MAIN
#define FW_VERSION_MAIN "V.UNKNOWN"
#endif

/* ---------- ESP32 Temperatur ---------- */
static float readEspTemperatureC() {
  float v = temperatureRead();
  // If it's clearly Fahrenheit (typical idle values 90–140 F), convert to °C
  if (v > 85.0f) return (v - 32.0f) / 1.8f;
  return v; // likely already °C on ESP32-S2/S3/C3
}

static String formatSessionTime(time_t value) {
  if (value <= 0) {
    return "";
  }

  struct tm timeinfo;
  localtime_r(&value, &timeinfo);

  char buffer[24];
  strftime(buffer, sizeof(buffer), "%d.%m.%Y %H:%M:%S", &timeinfo);
  return String(buffer);
}

OtaStatus otaMain{};
OtaStatus otaUi{};

SemaphoreHandle_t g_wsSubsMutex;   // schützt subscribedClients
static std::vector<uint8_t> g_pendingNetworkOnce;
static std::set<uint8_t> g_gridSettingsUnlockedClients;

std::map<uint8_t, std::string> subscribedClients;    // Client number with page

const char *WEB_TAG = "Task_Web: ";
static constexpr const char* RFID_AUTH_REQUIRED_KEY = "rfidAuthReq";
static constexpr const char* WEB_PASSWORD_KEY = "webPass";
static constexpr const char* WEB_SESSION_COOKIE = "ICSESSION";
static constexpr const char* SESSION_IMPORT_PATH = "/charge_sessions_import.json";
static constexpr size_t SESSION_PAGE_LIMIT = 100;
static volatile bool g_rebootRequested = false;
static volatile bool g_sessionImportPending = false;
static volatile size_t g_sessionPageOffset = 0;
static constexpr const char* GRID_SETTINGS_PIN = "2026";
static constexpr const char* GRID_PROFILE_KEY = "gridProfile";
static constexpr const char* GRID_NOMINAL_VOLTAGE_KEY = "gridNomVolt";
static constexpr const char* GRID_UV_PERCENT_KEY = "gridUvPct";
static constexpr const char* GRID_UV_TRIP_SECONDS_KEY = "gridUvTripS";
static constexpr const char* GRID_RECONNECT_SECONDS_KEY = "gridRecS";

struct SessionImportUpload {
    File file;
    bool failed = false;
};

// Initialization of webserver and websocket
AsyncWebServer server(80); // the server uses port 80 (standard port for websites

const char* www_username = "admin";
static String www_password_storage = "admin";
static constexpr size_t MAX_WEB_SESSIONS = 4;
static std::vector<String> g_webSessionTokens;

static constexpr const char* RECOVERY_HTML =
    "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "<title>InnoCharge Recovery</title>"
    "<style>"
    "body{font-family:Arial,sans-serif;background:#f2f2f6;margin:0;padding:32px}"
    ".box{background:#fff;padding:24px;max-width:520px;margin:auto;box-shadow:0 2px 8px #0002}"
    "input,button{box-sizing:border-box;width:100%;padding:10px;margin:10px 0;font-size:16px}"
    "button{background:#25343b;color:#fff;border:0;font-weight:bold}"
    "p{line-height:1.4}"
    "</style></head><body><div class=\"box\">"
    "<h1>InnoCharge Recovery</h1>"
    "<p>Upload a Web-UI firmware image if the normal web interface is not reachable.</p>"
    "<form method=\"POST\" action=\"/uploadui\" enctype=\"multipart/form-data\">"
    "<input type=\"hidden\" name=\"recovery\" value=\"1\">"
    "<input type=\"file\" name=\"update\" accept=\".bin\" required>"
    "<button type=\"submit\">Upload Web-UI Firmware</button>"
    "</form></div></body></html>";

static const char* current_web_password() {
    return rescueMode ? "admin" : www_password_storage.c_str();
}

static String make_web_session_token() {
    char token[33];
    snprintf(token, sizeof(token), "%08X%08X%08X%08X", esp_random(), esp_random(), esp_random(), esp_random());
    return String(token);
}

static String web_request_session_token(AsyncWebServerRequest* request) {
    if (!request->hasHeader("Cookie")) return "";

    AsyncWebHeader* cookie = request->getHeader("Cookie");
    if (!cookie) return "";

    String prefix = String(WEB_SESSION_COOKIE) + "=";
    String value = cookie->value();
    int start = value.indexOf(prefix);
    if (start < 0) return "";

    start += prefix.length();
    int end = value.indexOf(';', start);
    return end >= 0 ? value.substring(start, end) : value.substring(start);
}

static bool web_session_valid_token(const String& token) {
    if (token.length() == 0) return false;

    bool valid = false;
    if (g_wsSubsMutex && xSemaphoreTake(g_wsSubsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (const auto& sessionToken : g_webSessionTokens) {
            if (sessionToken == token) {
                valid = true;
                break;
            }
        }
        xSemaphoreGive(g_wsSubsMutex);
    }
    return valid;
}

static void add_web_session(const String& token) {
    if (token.length() == 0) return;

    if (g_wsSubsMutex && xSemaphoreTake(g_wsSubsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (auto it = g_webSessionTokens.begin(); it != g_webSessionTokens.end();) {
            if (*it == token) it = g_webSessionTokens.erase(it);
            else ++it;
        }

        while (g_webSessionTokens.size() >= MAX_WEB_SESSIONS) {
            g_webSessionTokens.erase(g_webSessionTokens.begin());
        }

        g_webSessionTokens.push_back(token);
        xSemaphoreGive(g_wsSubsMutex);
    }
}

static void remove_web_session(const String& token) {
    if (token.length() == 0) return;

    if (g_wsSubsMutex && xSemaphoreTake(g_wsSubsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (auto it = g_webSessionTokens.begin(); it != g_webSessionTokens.end();) {
            if (*it == token) it = g_webSessionTokens.erase(it);
            else ++it;
        }
        xSemaphoreGive(g_wsSubsMutex);
    }
}

bool web_request_has_session(AsyncWebServerRequest* request) {
    return web_session_valid_token(web_request_session_token(request));
}

static void redirect_to_login(AsyncWebServerRequest* request) {
    AsyncWebServerResponse* response = request->beginResponse(302);
    response->addHeader("Location", "/login.html");
    request->send(response);
}

static void send_protected_spiffs_file(AsyncWebServerRequest* request, const char* path) {
    if (!web_request_has_session(request)) {
        redirect_to_login(request);
        return;
    }
    request->send(SPIFFS, path, "text/html");
}

static void register_protected_page_routes(AsyncWebServer& server) {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        send_protected_spiffs_file(request, "/index.html");
    });

    const char* pages[] = {
        "/index.html",
        "/network.html",
        "/interfaces.html",
        "/rfid.html",
        "/sessions.html",
        "/grid_settings.html",
        "/system.html"
    };

    for (const char* page : pages) {
        server.on(page, HTTP_GET, [page](AsyncWebServerRequest* request) {
            send_protected_spiffs_file(request, page);
        });
    }
}

class CaptiveRequestHandler : public AsyncWebHandler {
public:
    CaptiveRequestHandler() {}
    virtual ~CaptiveRequestHandler() {}

    bool canHandle(AsyncWebServerRequest *request) {
        // This handler will handle all requests that are not found in SPIFFS
        return !SPIFFS.exists(request->url());
    }

    void handleRequest(AsyncWebServerRequest *request) {
        //ESP_LOGI(WEB_TAG, "Handling request for %s", request->url().c_str());
        File file = SPIFFS.open("/login.html", "r");
        if (!file) {
            // If the file cannot be opened, send a default response
        //    ESP_LOGE(WEB_TAG, "Failed to open /login.html");
            AsyncResponseStream *response = request->beginResponseStream("text/html");
            response->print("<!DOCTYPE html><html><head><title>Captive Portal</title></head><body>");
            response->print("<p>Failed to open file for reading.</p>");
            response->printf("<p>You were trying to reach: http://%s%s</p>", request->host().c_str(), request->url().c_str());
            response->printf("<p>Try opening <a href='http://%s'>this link</a> instead</p>", WiFi.softAPIP().toString().c_str());
            response->print("</body></html>");
            request->send(response);
        } else {
            // If the file is opened successfully, send its content
         //   ESP_LOGI(WEB_TAG, "Serving /login.html");
            AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/login.html", "text/html");
            request->send(response);
            file.close();
        }
    }
};

WebSocketsServer webSocket = WebSocketsServer(81);

static void send_web_password_status(uint8_t num, bool ok, const char* message) {
    JsonDocument response;
    response["type"] = "webPasswordStatus";
    response["ok"] = ok;
    response["message"] = message;
    String out;
    serializeJson(response, out);
    webSocket.sendTXT(num, out);
}

static void send_grid_settings_status(uint8_t num, const char* type, bool ok, const char* message) {
    JsonDocument response;
    response["type"] = type;
    response["ok"] = ok;
    response["message"] = message;
    String out;
    serializeJson(response, out);
    webSocket.sendTXT(num, out);
}

static void send_cp_diagnostic_log(uint8_t num) {
    String out = cp_diagnostic_log_to_json();
    webSocket.sendTXT(num, out);
}

static bool grid_settings_client_unlocked(uint8_t num) {
    bool unlocked = false;
    if (xSemaphoreTake(g_wsSubsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        unlocked = g_gridSettingsUnlockedClients.find(num) != g_gridSettingsUnlockedClients.end();
        xSemaphoreGive(g_wsSubsMutex);
    }
    return unlocked;
}

static void lock_grid_settings_client(uint8_t num) {
    if (xSemaphoreTake(g_wsSubsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_gridSettingsUnlockedClients.erase(num);
        xSemaphoreGive(g_wsSubsMutex);
    }
}

static void unlock_grid_settings_client(uint8_t num) {
    if (xSemaphoreTake(g_wsSubsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_gridSettingsUnlockedClients.insert(num);
        xSemaphoreGive(g_wsSubsMutex);
    }
}

static bool websocket_session_valid(JsonDocument& doc) {
    if (!doc["session"].is<const char*>()) return false;
    return web_session_valid_token(String(doc["session"].as<const char*>()));
}

static bool websocket_public_app_allowed(JsonDocument& doc) {
    if (!doc["client"].is<const char*>() ||
        strcmp(doc["client"].as<const char*>(), "innocharge-public-app") != 0) {
        return false;
    }

    const char* action = doc["action"] | "";
    if (strcmp(action, "subscribeUpdates") == 0 || strcmp(action, "unsubscribeUpdates") == 0) {
        return doc["page"].is<const char*>() && strcmp(doc["page"].as<const char*>(), "app") == 0;
    }

    return strcmp(action, "setChargeParameters") == 0 && doc.containsKey("power");
}

static String build_grid_settings_json() {
    JsonDocument doc;
    uint16_t nominalVoltage = preferences.getUShort(GRID_NOMINAL_VOLTAGE_KEY, 230);
    uint8_t undervoltagePercent = preferences.getUChar(GRID_UV_PERCENT_KEY, 80);
    float minVoltage = grid_protection_get_min_voltage(sdm.voltL1, sdm.voltL2, sdm.voltL3);

    doc["gridProfile"] = preferences.getUChar(GRID_PROFILE_KEY, 0);
    doc["gridNominalVoltage"] = nominalVoltage;
    doc["gridUndervoltagePercent"] = undervoltagePercent;
    doc["gridUndervoltageVolts"] = roundf(grid_protection_get_trip_voltage(nominalVoltage, undervoltagePercent) * 10.0f) / 10.0f;
    doc["gridUndervoltageTripSeconds"] = preferences.getUShort(GRID_UV_TRIP_SECONDS_KEY, 3);
    doc["gridReconnectDelaySeconds"] = preferences.getUShort(GRID_RECONNECT_SECONDS_KEY, 60);
    doc["gridReconnectDelayRemainingSeconds"] = gridReconnectDelayRemainingSeconds;
    doc["gridProtectionStatus"] = gridProtectionStatus;
    doc["gridReconnectRampActive"] = gridReconnectRampActive;
    doc["gridReconnectRampLimitPower"] = (int16_t)roundf(gridReconnectRampLimitPower);
    doc["gridPhaseImbalanceDetected"] = grid_protection_phase_imbalance_active();
    doc["gridPhaseImbalanceLimitActive"] = gridPhaseImbalanceLimitActive;
    doc["gridPhaseImbalanceLimitRemainingSeconds"] = gridPhaseImbalanceLimitRemainingSeconds;
    doc["energyMeterState"] = preferences.getBool("emEnable", false);
    doc["energyMeterType"] = preferences.getUChar("emType", EnergyMeter_EastronSdm630);
    doc["energyMeterModbusId"] = sdm.modbusId;
    doc["energyMeterError"] = sdm.error;
    doc["l1Voltage"] = (int16_t)roundf(sdm.voltL1 * 10);
    doc["l2Voltage"] = (int16_t)roundf(sdm.voltL2 * 10);
    doc["l3Voltage"] = (int16_t)roundf(sdm.voltL3 * 10);
    doc["frequency"] = (int16_t)roundf(sdm.frequency * 100);
    doc["minGridVoltage"] = (int16_t)roundf(minVoltage * 10);
    doc["l1VoltageOk"] = grid_protection_voltage_reconnect_ok(nominalVoltage, sdm.voltL1);
    doc["l2VoltageOk"] = grid_protection_voltage_reconnect_ok(nominalVoltage, sdm.voltL2);
    doc["l3VoltageOk"] = grid_protection_voltage_reconnect_ok(nominalVoltage, sdm.voltL3);
    doc["frequencyOk"] = grid_protection_frequency_reconnect_ok(sdm.frequency);

    String out;
    serializeJson(doc, out);
    return out;
}

static bool save_grid_setting_value(const char* key, int value) {
    // Speichert genau einen Grid-Parameter, damit andere Felder nicht ueberschrieben werden.
    if (strcmp(key, "profile") == 0) {
        preferences.putUChar(GRID_PROFILE_KEY, (uint8_t)value);
        return true;
    }
    if (strcmp(key, "undervoltagePercent") == 0) {
        preferences.putUChar(GRID_UV_PERCENT_KEY, (uint8_t)value);
        return true;
    }
    if (strcmp(key, "undervoltageTripSeconds") == 0) {
        preferences.putUShort(GRID_UV_TRIP_SECONDS_KEY, (uint16_t)value);
        return true;
    }
    if (strcmp(key, "reconnectDelaySeconds") == 0) {
        preferences.putUShort(GRID_RECONNECT_SECONDS_KEY, (uint16_t)value);
        return true;
    }
    return false;
}

void webSocketEvent(byte num, WStype_t type, uint8_t * payload, size_t length) {
    switch (type) {
	        case WStype_DISCONNECTED:
	            if (xSemaphoreTake(g_wsSubsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
	                subscribedClients.erase(num);
	                g_gridSettingsUnlockedClients.erase(num);
	                xSemaphoreGive(g_wsSubsMutex);
	            }
            ESP_LOGI(WEB_TAG, "Client %s disconnected", String(num).c_str());
            break;
        case WStype_CONNECTED:
            ESP_LOGI(WEB_TAG, "Client %s connected", String(num).c_str());
            break;
        case WStype_TEXT:

            // ESP_LOGI(WEB_TAG, "Received WebSocket message.");
            // Ausgabe des rohen Payloads zur Überprüfung
            // ESP_LOGI(WEB_TAG, "Raw payload data: %.*s", length, (char*)payload);

            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);
            if (error) {
                ESP_LOGI(WEB_TAG, "JSON parse error: %s", error.c_str());
                return;
            } else {
                JsonVariantConst val;
                val = doc["action"];
	                if (!val.isNull()) {
	                    const char* action = val.as<const char*>();

                    if (!websocket_session_valid(doc) && !websocket_public_app_allowed(doc)) {
                        ESP_LOGW(WEB_TAG, "Rejected WebSocket action without valid session: %s", action);
                        return;
                    }

                // NEU: One-Shot Network-Info anfordern (kein Subscribe)
                    if (strcmp(action, "requestNetworkInfo") == 0) {
                        if (xSemaphoreTake(g_wsSubsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                            // Deduplizieren: Client nur einmal einreihen
                            if (std::find(g_pendingNetworkOnce.begin(),
                                        g_pendingNetworkOnce.end(), num) == g_pendingNetworkOnce.end()) {
                                g_pendingNetworkOnce.push_back(num);
                            }
                            xSemaphoreGive(g_wsSubsMutex);
                        }
                        return; // hier nichts weiter tun
                    }

                    if (strcmp(action, "subscribeUpdates") == 0) {
                        const char* page = doc["page"];
                        if (xSemaphoreTake(g_wsSubsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                            subscribedClients[num] = std::string(page);
                            xSemaphoreGive(g_wsSubsMutex);
                        }
	                  //      ESP_LOGI(WEB_TAG, "Client %u subscribed to updates", num);
                    } else if (strcmp(action, "unsubscribeUpdates") == 0) {
                        if (xSemaphoreTake(g_wsSubsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                            subscribedClients.erase(num);
                            xSemaphoreGive(g_wsSubsMutex);
                        }
                  //      ESP_LOGI(WEB_TAG, "Client %u unsubscribed from updates", num);
                    } else if (strcmp(action, "startCpDiagnosticLog") == 0) {
                        cp_diagnostic_log_start();
                        send_cp_diagnostic_log(num);
                    } else if (strcmp(action, "stopCpDiagnosticLog") == 0) {
                        cp_diagnostic_log_stop();
                        send_cp_diagnostic_log(num);
                    } else if (strcmp(action, "clearCpDiagnosticLog") == 0) {
                        cp_diagnostic_log_clear();
                        send_cp_diagnostic_log(num);
                    } else if (strcmp(action, "getCpDiagnosticLog") == 0) {
                        send_cp_diagnostic_log(num);
                    } else if (strcmp(action, "setCpRelayState") == 0 && doc["state"].is<bool>()) {
                        bool state = doc["state"].as<bool>();
                        state ? turn_on_cp_relay() : turn_off_cp_relay();
                    } else if (strcmp(action, "setDelayedPhaseSwitchingSeconds") == 0 && doc["seconds"].is<int>()) {
                        int seconds = doc["seconds"].as<int>();
                        if (seconds < 0) seconds = 0;
                        if (seconds > 3600) seconds = 3600;
                        delayedPhaseSwitchingSeconds = (uint16_t)seconds;
                        preferences.putUShort("delayed1p3pS", delayedPhaseSwitchingSeconds);
	                    } else if (strcmp(action, "setChargeParameters") == 0 && doc.containsKey("current")) {
	                        int current = doc["current"].as<int>();
	                        set_charging_current_external(current);
		                    } else if (strcmp(action, "setChargeParameters") == 0 && doc.containsKey("power")) {
		                        float power = doc["power"].as<float>();
		                        set_charging_power_external(power * 10);
		                    } else if (strcmp(action, "saveDynamicPowerLimitRow") == 0 && doc["index"].is<int>()) {
	                        int index = doc["index"].as<int>();
	                        bool enabled = doc["enabled"] | false;
	                        String config = doc["config"] | "";
	                        dynamic_power_limit_save_row((uint8_t)index, enabled, config);
	                    } else if (strcmp(action, "unlockGridSettings") == 0 && doc["pin"].is<const char*>()) {
	                        const char* pin = doc["pin"];
	                        if (strcmp(pin, GRID_SETTINGS_PIN) == 0) {
	                            unlock_grid_settings_client(num);
	                            send_grid_settings_status(num, "gridSettingsAuth", true, "Unlocked");
	                            String gridJson = build_grid_settings_json();
	                            webSocket.sendTXT(num, gridJson);
	                        } else {
	                            lock_grid_settings_client(num);
	                            send_grid_settings_status(num, "gridSettingsAuth", false, "Wrong PIN");
	                        }
		                    } else if (strcmp(action, "lockGridSettings") == 0) {
		                        lock_grid_settings_client(num);
		                    } else if (strcmp(action, "saveGridSetting") == 0 && doc["key"].is<const char*>()) {
		                        if (!grid_settings_client_unlocked(num)) {
		                            send_grid_settings_status(num, "gridSettingsSaveStatus", false, "PIN required");
		                        } else if (save_grid_setting_value(doc["key"].as<const char*>(), doc["value"] | 0)) {
		                            send_grid_settings_status(num, "gridSettingsSaveStatus", true, "Saved");
		                            String gridJson = build_grid_settings_json();
		                            webSocket.sendTXT(num, gridJson);
		                        } else {
		                            send_grid_settings_status(num, "gridSettingsSaveStatus", false, "Unknown setting");
		                        }
		                    } else if (strcmp(action, "saveGridSettings") == 0 && doc["settings"].is<JsonObject>()) {
		                        if (!grid_settings_client_unlocked(num)) {
		                            send_grid_settings_status(num, "gridSettingsSaveStatus", false, "PIN required");
	                        } else {
	                            JsonObjectConst settings = doc["settings"].as<JsonObjectConst>();
	                            preferences.putUChar(GRID_PROFILE_KEY, settings["profile"] | 0);
	                            preferences.putUShort(GRID_NOMINAL_VOLTAGE_KEY, settings["nominalVoltage"] | 230);
	                            preferences.putUChar(GRID_UV_PERCENT_KEY, settings["undervoltagePercent"] | 80);
	                            preferences.putUShort(GRID_UV_TRIP_SECONDS_KEY, settings["undervoltageTripSeconds"] | 3);
	                            preferences.putUShort(GRID_RECONNECT_SECONDS_KEY, settings["reconnectDelaySeconds"] | 60);
	                            send_grid_settings_status(num, "gridSettingsSaveStatus", true, "Saved");
	                            String gridJson = build_grid_settings_json();
	                            webSocket.sendTXT(num, gridJson);
	                        }
	                        // Page-Interfaces
			                    } else if (strcmp(action, "setEnergyMeter") == 0 && doc["state"].is<bool>()) {
	                        bool state = doc["state"].as<bool>();
	                        preferences.putBool("emEnable", state);  
	                        preferences.putUChar(GRID_PROFILE_KEY, state ? 1 : 0);
	                        if (state) {
	                            preferences.putUChar(GRID_UV_PERCENT_KEY, 80);
	                            preferences.putUShort(GRID_UV_TRIP_SECONDS_KEY, 3);
	                            preferences.putUShort(GRID_RECONNECT_SECONDS_KEY, 60);
	                        }
	                        sdm.enable = state;
	                      //  sdm.error = state;
	                    } else if (strcmp(action, "setEnergyMeterType") == 0 && doc["type"].is<int>()) {
	                        int type = doc["type"].as<int>();
	                        if (type < EnergyMeter_EastronSdm630 || type > EnergyMeter_YtDts353F2) {
	                            type = EnergyMeter_EastronSdm630;
	                        }
	                        sdm.type = (energy_meter_type_t)type;
	                        preferences.putUChar("emType", (uint8_t)sdm.type);
	                    } else if (strcmp(action, "setEnergyMeterModbusId") == 0 && doc["address"].is<int>()) {
	                        int address = doc["address"].as<int>();
	                        if (address < 1 || address > 247) address = 1;
	                        sdm.modbusId = (uint8_t)address;
	                        preferences.putUChar("emMbId", sdm.modbusId);
	                    } else if (strcmp(action, "setEnergySign") == 0 && doc["state"].is<bool>()) {
	                        bool state = doc["state"].as<bool>();
	                        preferences.putBool("emSignEnable", state); 
	                        sdm.invSign = state;    
		                    } else if (strcmp(action, "setWallboxName") == 0 && doc["name"].is<const char*>()) {
		                        String name = doc["name"].as<String>();
		                        name.trim();
		                        if (name.length() > 32) {
		                            name = name.substring(0, 32);
		                        }
		                        preferences.putString("wallboxName", name);
		                    } else if (strcmp(action, "setWebPassword") == 0 && doc["password"].is<const char*>()) {
		                        String password = doc["password"].as<String>();
		                        password.trim();
		                        if (password.length() < 4) {
		                            send_web_password_status(num, false, "Password must contain at least 4 characters.");
		                        } else if (password.length() > 32) {
		                            send_web_password_status(num, false, "Password must contain 32 characters or less.");
		                        } else {
		                            preferences.putString(WEB_PASSWORD_KEY, password);
			                            if (!rescueMode) {
			                                www_password_storage = password;
			                            }
			                            send_web_password_status(num, true, "Password saved. Reboot required.");
			                        }
			                    } else if (strcmp(action, "setRfid") == 0 && doc["state"].is<bool>()) {
		                        bool state = doc["state"].as<bool>();
		                        preferences.putBool("rfidEnable", state); 
		                        rfid.enable = state;
		                       // rfid.error = state;
		                       // ESP_LOGI(WEB_TAG, "energyMeterEnable = %d", preferences.getBool("rfidEnable", false));
			                    } else if (strcmp(action, "setRfidModbusId") == 0 && doc["address"].is<int>()) {
			                        int address = doc["address"].as<int>();
			                        if (address < 1 || address > 247) address = 2;
			                        rfid.modbusId = (uint8_t)address;
			                        preferences.putUChar("rfidMbId", rfid.modbusId);
			                    } else if (strcmp(action, "setRfidBuzzer") == 0 && doc["state"].is<bool>()) {
			                        rfid.buzzer = doc["state"].as<bool>();
			                        preferences.putBool("rfidBuzzer", rfid.buzzer);
			                    } else if (strcmp(action, "setRfidLed") == 0 && doc["value"].is<int>()) {
			                        int value = doc["value"].as<int>();
			                        if (value < 0 || value > 2) value = 0;
			                        rfid.led = (uint8_t)value;
			                        preferences.putUChar("rfidLed", rfid.led);
			                    } else if (strcmp(action, "saveRfidUser") == 0) {
	                        rfid_user_t user;
		                        user.idTag = doc["idTag"] | "";
		                        user.name = doc["name"] | "";
		                        user.enabled = doc["enabled"] | true;
		                        int maxChargeMinutes = doc["maxChargeMinutes"] | 0;
		                        if (maxChargeMinutes < 0) maxChargeMinutes = 0;
		                        if (maxChargeMinutes > 65535) maxChargeMinutes = 65535;
		                        user.maxChargeMinutes = (uint16_t)maxChargeMinutes;
		                        user.note = doc["note"] | "";
		                        rfid_db_upsert_user(user);
	                    } else if (strcmp(action, "deleteRfidUser") == 0) {
	                        const char* idTag = doc["idTag"] | "";
	                        rfid_db_delete_user(idTag);
				                    } else if (strcmp(action, "setRfidAuthorizationRequired") == 0 && doc["state"].is<bool>()) {
				                        bool state = doc["state"].as<bool>();
				                        preferences.putBool(RFID_AUTH_REQUIRED_KEY, state);
				                        rfidAuth.required = state;
					                    } else if (strcmp(action, "importChargeSessions") == 0 && doc["data"].is<const char*>()) {
					                        charge_session_log_import_json(doc["data"].as<String>());
					                    } else if (strcmp(action, "requestSessionPage") == 0) {
					                        int offset = doc["offset"] | 0;
					                        if (offset < 0) offset = 0;
					                        g_sessionPageOffset = (size_t)offset;
					                    } else if (strcmp(action, "saveSessionMailerSettings") == 0 && doc["settings"].is<JsonObject>()) {
					                        session_mailer_save_settings(doc["settings"].as<JsonObjectConst>());
				                    } else if (strcmp(action, "sendSessionReportNow") == 0) {
				                        session_mailer_send_manual_report();
				                    } else if (strcmp(action, "rebootDevice") == 0) {
			                        g_rebootRequested = true;
			                    }
                }

                // Verarbeitung der anderen JSON-Felder außerhalb des 'if (!val.isNull())'-Blocks

                // Safely handle array Ethernet conversion for each setting
                if (doc.containsKey("setEthIpAdr") && doc["setEthIpAdr"].is<JsonArray>()) {
                    JsonArray arr = doc["setEthIpAdr"].as<JsonArray>();
                    uint8_t ip[4];
                    for (size_t i = 0; i < 4; i++) ip[i] = arr[i];
                    preferences.putBytes("eth_ip_addr", ip, sizeof(ip));
                }

                if (doc.containsKey("setEthNetmask") && doc["setEthNetmask"].is<JsonArray>()) {
                    JsonArray arr = doc["setEthNetmask"].as<JsonArray>();
                    uint8_t netmask[4];
                    for (size_t i = 0; i < 4; i++) netmask[i] = arr[i];
                    preferences.putBytes("eth_netmask", netmask, sizeof(netmask));
                }

                if (doc.containsKey("setEthGw") && doc["setEthGw"].is<JsonArray>()) {
                    JsonArray arr = doc["setEthGw"].as<JsonArray>();
                    uint8_t gw[4];
                    for (size_t i = 0; i < 4; i++) gw[i] = arr[i];
                    preferences.putBytes("eth_gw", gw, sizeof(gw));
                }

                if (doc.containsKey("setEthDns1") && doc["setEthDns1"].is<JsonArray>()) {
                    JsonArray arr = doc["setEthDns1"].as<JsonArray>();
                    uint8_t dns1[4];
                    for (size_t i = 0; i < 4; i++) dns1[i] = arr[i];
                    preferences.putBytes("eth_dns1", dns1, sizeof(dns1));
                }

                if (doc.containsKey("setEthDns2") && doc["setEthDns2"].is<JsonArray>()) {
                    JsonArray arr = doc["setEthDns2"].as<JsonArray>();
                    uint8_t dns2[4];
                    for (size_t i = 0; i < 4; i++) dns2[i] = arr[i];
                    preferences.putBytes("eth_dns2", dns2, sizeof(dns2));
                }

                if (doc.containsKey("setWifiSSID") && doc["setWifiSSID"].is<const char*>()) {
                    const char* ssid_str = doc["setWifiSSID"].as<const char*>();
                    char ssid[32] = {0}; 
                    strncpy(ssid, ssid_str, sizeof(ssid) - 1); 
                    preferences.putBytes("wifi_ssid", ssid, sizeof(ssid)); 
                }

                if (doc.containsKey("setWifiPassword") && doc["setWifiPassword"].is<const char*>()) {
                    const char* pwd_str = doc["setWifiPassword"].as<const char*>();
                    char pwd[64] = {0}; 
                    strncpy(pwd, pwd_str, sizeof(pwd) - 1); 
                    preferences.putBytes("wifi_pwd", pwd, sizeof(pwd)); 
                }

                if (doc.containsKey("setWifiIpAdr") && doc["setWifiIpAdr"].is<JsonArray>()) {
                    JsonArray arr = doc["setWifiIpAdr"].as<JsonArray>();
                    uint8_t ip[4];
                    for (size_t i = 0; i < 4; i++) ip[i] = arr[i];
                    preferences.putBytes("wifi_ip_addr", ip, sizeof(ip));
                }

                if (doc.containsKey("setWifiNetmask") && doc["setWifiNetmask"].is<JsonArray>()) {
                    JsonArray arr = doc["setWifiNetmask"].as<JsonArray>();
                    uint8_t netmask[4];
                    for (size_t i = 0; i < 4; i++) netmask[i] = arr[i];
                    preferences.putBytes("wifi_netmask", netmask, sizeof(netmask));
                }

                if (doc.containsKey("setWifiGw") && doc["setWifiGw"].is<JsonArray>()) {
                    JsonArray arr = doc["setWifiGw"].as<JsonArray>();
                    uint8_t gw[4];
                    for (size_t i = 0; i < 4; i++) gw[i] = arr[i];
                    preferences.putBytes("wifi_gw", gw, sizeof(gw));
                }

                if (doc.containsKey("setWifiDns1") && doc["setWifiDns1"].is<JsonArray>()) {
                    JsonArray arr = doc["setWifiDns1"].as<JsonArray>();
                    uint8_t dns1[4];
                    for (size_t i = 0; i < 4; i++) dns1[i] = arr[i];
                    preferences.putBytes("wifi_dns1", dns1, sizeof(dns1));
                }

                if (doc.containsKey("setWifiDns2") && doc["setWifiDns2"].is<JsonArray>()) {
                    JsonArray arr = doc["setWifiDns2"].as<JsonArray>();
                    uint8_t dns2[4];
                    for (size_t i = 0; i < 4; i++) dns2[i] = arr[i];
                    preferences.putBytes("wifi_dns2", dns2, sizeof(dns2));
                }

                // Set the new Ethernet mode (static or dynamic) and restart Ethernet
                val = doc["setEthStatic"];
                if (val != nullptr) {
                    preferences.putBool("ethStatic", val.as<bool>());                 
                    esp_restart();
                }

                val = doc["setWifiEnable"];
                if (val != nullptr) {
                    preferences.putBool("wifiEnable", val.as<bool>());
                }

                // Set the new Wifi mode (static or dynamic) and restart Ethernet
                val = doc["setWifiStatic"];
                if (val != nullptr) {
                    preferences.putBool("wifiStatic", val.as<bool>());
                    esp_restart();
                }
              
                 String jsonString;
                 serializeJsonPretty(doc, jsonString);  // Speichert das JSON im String jsonString
               //  ESP_LOGI(WEB_TAG, "Received JSON data:\n%s", jsonString.c_str());
            }
            break;
    }
}


static void Task_WebPush(void* arg) {
  // Caches der letzten JSONs je Seite (um identische Frames nicht zu spammen)
	  String lastIndex, lastInterfaces, lastSystem, lastRfid, lastSessions, lastGridSettings;

  for (;;) {

    // === A) One-Shot: pending Network-Infos verschicken, falls angefragt ===
    {
      std::vector<uint8_t> pending;
      if (xSemaphoreTake(g_wsSubsMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        pending.swap(g_pendingNetworkOnce);  // Liste leeren & lokale Kopie holen
        xSemaphoreGive(g_wsSubsMutex);
      }

      if (!pending.empty()) {

        ethernet_state_t eth;
        get_ethernet_state(&eth);

        wifi_sta_state_t wifi;
        get_wifi_sta_state(&wifi);

	        StaticJsonDocument<768> doc;
        char mac[18];
        char ip [16];

        // --- ETH ---
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 eth.mac_addr[0], eth.mac_addr[1], eth.mac_addr[2],
                 eth.mac_addr[3], eth.mac_addr[4], eth.mac_addr[5]);
        doc["eth_mac"] = mac;

        snprintf(ip, sizeof(ip), "%u.%u.%u.%u", eth.ip_addr[0], eth.ip_addr[1], eth.ip_addr[2], eth.ip_addr[3]);
        doc["eth_ip"] = ip;
        snprintf(ip, sizeof(ip), "%u.%u.%u.%u", eth.netmask[0], eth.netmask[1], eth.netmask[2], eth.netmask[3]);
        doc["eth_netmask"] = ip;
        snprintf(ip, sizeof(ip), "%u.%u.%u.%u", eth.gw[0], eth.gw[1], eth.gw[2], eth.gw[3]);
        doc["eth_gateway"] = ip;
        snprintf(ip, sizeof(ip), "%u.%u.%u.%u", eth.dns1[0], eth.dns1[1], eth.dns1[2], eth.dns1[3]);
        doc["eth_dns1"] = ip;
        snprintf(ip, sizeof(ip), "%u.%u.%u.%u", eth.dns2[0], eth.dns2[1], eth.dns2[2], eth.dns2[3]);
        doc["eth_dns2"] = ip;
        doc["eth_static"]  = preferences.getBool("ethStatic", false);

        // --- WiFi (nur aus Status-Struktur, KEINE esp_wifi_* Aufrufe hier!) ---
        // --- RSSI & Qualitäts-Prozent (nur wenn verbunden) ---
        int8_t rssi = -127;          // -127 als "unbekannt"
        int quality = 0;

        if (wifi.connected) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            rssi = ap.rssi;  // dBm, typ. -30 .. -90
            // 0..100% aus dBm: -100→0%, -50→100% (linear)
            quality = (rssi <= -100) ? 0 :
                    (rssi >= -50)  ? 100 :
                    2 * (rssi + 100);
        }
        }
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 wifi.mac[0], wifi.mac[1], wifi.mac[2],
                 wifi.mac[3], wifi.mac[4], wifi.mac[5]);
        doc["wifi_mac"]        = mac;
        doc["wifi_ssid"]       = (const char*)wifi.ssid;
        doc["wifi_pwd"]       = (const char*)wifi.passphrase;
        doc["wifi_rssi"]    = rssi;     // z.B. -63
        doc["wifi_signal"] = quality;  // z.B. 74 (in %)

        snprintf(ip, sizeof(ip), "%u.%u.%u.%u", wifi.ip[0], wifi.ip[1], wifi.ip[2], wifi.ip[3]);
        doc["wifi_ip"] = ip;
        snprintf(ip, sizeof(ip), "%u.%u.%u.%u", wifi.netmask[0], wifi.netmask[1], wifi.netmask[2], wifi.netmask[3]);
        doc["wifi_netmask"] = ip;
        snprintf(ip, sizeof(ip), "%u.%u.%u.%u", wifi.gateway[0], wifi.gateway[1], wifi.gateway[2], wifi.gateway[3]);
        doc["wifi_gateway"] = ip;
        snprintf(ip, sizeof(ip), "%u.%u.%u.%u", wifi.dns1[0], wifi.dns1[1], wifi.dns1[2], wifi.dns1[3]);
        doc["wifi_dns1"] = ip;
        snprintf(ip, sizeof(ip), "%u.%u.%u.%u", wifi.dns2[0], wifi.dns2[1], wifi.dns2[2], wifi.dns2[3]);
        doc["wifi_dns2"] = ip;

	        doc["wifi_static"]    = preferences.getBool("wifiStatic", false);
	        doc["wifi_connected"] = wifi.connected;
	        doc["wifi_enable"]    = preferences.getBool("wifiEnable", false);
	        doc["rescueMode"]     = rescueMode;

/*

// --- Strings für Log (separat, damit wir sie im Log gemeinsam ausgeben können)
char wifiIpStr[16], wifiMaskStr[16], wifiGwStr[16], wifiDns1Str[16], wifiDns2Str[16], wifiMacStr[18];
snprintf(wifiIpStr,   sizeof(wifiIpStr),   "%u.%u.%u.%u", wifi.ip[0],      wifi.ip[1],      wifi.ip[2],      wifi.ip[3]);
snprintf(wifiMaskStr, sizeof(wifiMaskStr), "%u.%u.%u.%u", wifi.netmask[0], wifi.netmask[1], wifi.netmask[2], wifi.netmask[3]);
snprintf(wifiGwStr,   sizeof(wifiGwStr),   "%u.%u.%u.%u", wifi.gateway[0], wifi.gateway[1], wifi.gateway[2], wifi.gateway[3]);
snprintf(wifiDns1Str, sizeof(wifiDns1Str), "%u.%u.%u.%u", wifi.dns1[0],    wifi.dns1[1],    wifi.dns1[2],    wifi.dns1[3]);
snprintf(wifiDns2Str, sizeof(wifiDns2Str), "%u.%u.%u.%u", wifi.dns2[0],    wifi.dns2[1],    wifi.dns2[2],    wifi.dns2[3]);
snprintf(wifiMacStr,  sizeof(wifiMacStr),  "%02X:%02X:%02X:%02X:%02X:%02X",
         wifi.mac[0], wifi.mac[1], wifi.mac[2], wifi.mac[3], wifi.mac[4], wifi.mac[5]);

// --- Logausgabe (Passwort NICHT loggen)
ESP_LOGI(WEB_TAG,
         "WiFi: enable=%d connected=%d SSID='%s' MAC=%s RSSI=%d dBm quality=%d%% "
         "IP=%s MASK=%s GW=%s DNS1=%s DNS2=%s DHCP=%d",
         (int)preferences.getBool("wifiEnable", false),
         (int)wifi.connected,
         (const char*)wifi.ssid,
         wifiMacStr,
         (int)rssi,
         (int)quality,
         wifiIpStr, wifiMaskStr, wifiGwStr, wifiDns1Str, wifiDns2Str,
         (int)preferences.getBool("wifiStatic", false));*/

        String jsonOnce; jsonOnce.reserve(512);
        serializeJson(doc, jsonOnce);

        for (auto id : pending) {
          webSocket.sendTXT(id, jsonOnce);   // EINMAL an alle Anfragenden
        }
      }
    }
    // === Ende One-Shot ===


    // --- 1) Thread-sicher Kopie der Abos ziehen ---
    std::vector<std::pair<uint8_t, std::string>> clients;
    if (xSemaphoreTake(g_wsSubsMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      clients.assign(subscribedClients.begin(), subscribedClients.end());
      xSemaphoreGive(g_wsSubsMutex);
    }

    // --- 2) Bedarf je Seite feststellen ---
		    bool needIndex = false, needInterfaces = false, needSystem = false, needRfid = false, needSessions = false, needGridSettings = false, needApp = false;
		    for (auto &c : clients) {
			      if      (c.second == "index")      needIndex      = true;
		      else if (c.second == "app")        needApp        = true;
		      else if (c.second == "interfaces") needInterfaces = true;
	      else if (c.second == "system")     needSystem     = true;
	      else if (c.second == "rfid")       needRfid       = true;
	      else if (c.second == "sessions")   needSessions   = true;
	      else if (c.second == "grid_settings" && grid_settings_client_unlocked(c.first)) needGridSettings = true;
	    }

    // --- 3) JSONs je Seite GENAU EINMAL bauen (nur wenn benötigt) ---
		    String jsonIndex, jsonInterfaces, jsonSystem, jsonRfid, jsonSessions, jsonGridSettings, jsonApp;

			    if (needIndex) {
		      bool cpDuty100 = (currentCpState.state == StateCustom_DutyCycle_100) || (getCpDuty >= 99.5f);
		      StaticJsonDocument<2048> doc;
	      doc["wallboxName"]         = preferences.getString("wallboxName", "InnoCharge");
	      doc["cpState"]             = cpStateToName(currentCpState.state);
      doc["cpVoltage"]           = round(highVoltage * 10) / 10.0;
      doc["espTemp"]             = round(readEspTemperatureC() * 10) / 10.0;
      doc["phaseMode"] = currentCpState.threePhaseActive ? "Three-phase" : "Single-phase";
      doc["targetChargeCurrent"] = cpDuty100 ? 0 : (int)round(get_current_from_duty(getCpDuty));
	      doc["targetChargePower"]   = cpDuty100 ? 0.0f : round(get_power_from_duty(getCpDuty)) / 10.0;
		      doc["cpRelayState"]        = get_cp_relays_status();
		      doc["gridProfile"]         = preferences.getUChar(GRID_PROFILE_KEY, 0);
		      doc["gridProtectionStatus"] = gridProtectionStatus;
		      doc["gridReconnectDelayRemainingSeconds"] = gridReconnectDelayRemainingSeconds;
		      doc["gridReconnectRampLimitPower"] = (int16_t)roundf(gridReconnectRampLimitPower);
		      doc["gridPhaseImbalanceLimitRemainingSeconds"] = gridPhaseImbalanceLimitRemainingSeconds;
		      doc["delayedPhaseSwitchingSeconds"] = delayedPhaseSwitchingSeconds;
	      doc["phaseSwitchDelayRemainingSeconds"] = phaseSwitchDelayRemainingSeconds;
	      dynamic_power_limit_append_json(doc.as<JsonObject>());
		      jsonIndex.reserve(1024);
	      serializeJson(doc, jsonIndex);
	    }

    if (needInterfaces) {
		      StaticJsonDocument<640> doc;
		      doc["energyMeterState"] = preferences.getBool("emEnable", false);
		      doc["energyMeterType"]  = preferences.getUChar("emType", EnergyMeter_EastronSdm630);
		      doc["energyMeterModbusId"] = sdm.modbusId;
		      doc["energySignState"]  = preferences.getBool("emSignEnable", false);
      doc["l1Voltage"] =  (int16_t)roundf(sdm.voltL1 * 10);
      doc["l2Voltage"] =  (int16_t)roundf(sdm.voltL2 * 10);
      doc["l3Voltage"] =  (int16_t)roundf(sdm.voltL3 * 10);
      doc["frequency"] =  (int16_t)roundf(sdm.frequency * 100);
      doc["l1Current"] =  (int16_t)roundf(sdm.currL1 * 10);
      doc["l2Current"] =  (int16_t)roundf(sdm.currL2 * 10);
      doc["l3Current"] =  (int16_t)roundf(sdm.currL3 * 10);
      doc["l1Power"]   =  (int32_t)roundf(sdm.pwrL1  * 10);
      doc["l2Power"]   =  (int32_t)roundf(sdm.pwrL2  * 10);
      doc["l3Power"]   =  (int32_t)roundf(sdm.pwrL3  * 10);
      doc["totPower"]  =  (int32_t)roundf(sdm.pwrTot * 10);
      doc["impPower"]  =  (int32_t)roundf(sdm.enrImp * 100);
      doc["expPower"]  =  (int32_t)roundf(sdm.enrExp * 100);
      doc["energyMeterError"] = sdm.error;
      doc["rfidState"]        = preferences.getBool("rfidEnable", false);
      doc["rfidModbusId"]     = rfid.modbusId;
      doc["rfidTag"]          = rfid.uidStr;
      doc["lastRfidTag"]      = rfid.lastUidStr;
      doc["rfidError"]        = rfid.error;
      jsonInterfaces.reserve(384);
      serializeJson(doc, jsonInterfaces);
    }

			    if (needSystem) {
				      StaticJsonDocument<640> doc;
					      doc["wallboxName"]     = preferences.getString("wallboxName", "InnoCharge");
				      doc["gridProfile"]     = preferences.getUChar(GRID_PROFILE_KEY, 0);
				      doc["dipSwitch1"]      = (digitalRead(DIP_SWITCH_1) == LOW);
			      doc["dipSwitch2"]      = (digitalRead(DIP_SWITCH_2) == LOW);
			      doc["otaMainProgress"] = otaMain.progress;
	      doc["otaMainCode"]     = otaMain.code;
	      doc["otaMainMessage"]  = otaMain.message;
	      doc["otaMainVersion"]  = FW_VERSION_MAIN;
	      doc["otaUiProgress"]   = otaUi.progress;
	      doc["otaUiCode"]       = otaUi.code;
		      doc["otaUiMessage"]    = otaUi.message;
		      doc["localTime"]       = time_service_local_string();
		      jsonSystem.reserve(256);
	      serializeJson(doc, jsonSystem);
	    }

	    if (needRfid) {
	      JsonDocument doc;
	      String dbJson = rfid_db_to_json();
	      doc["rfidState"]        = preferences.getBool("rfidEnable", false);
	      doc["rfidModbusId"]     = rfid.modbusId;
	      doc["rfidBuzzer"]       = rfid.buzzer;
	      doc["rfidLed"]          = rfid.led;
	      doc["rfidTag"]          = rfid.uidStr;
	      doc["lastRfidTag"]      = rfid.lastUidStr;
	      doc["rfidError"]        = rfid.error;
	      rfid_user_t currentUser;
			      rfidAuth.authorized     = rfid_db_is_authorized(rfid.uidStr, &currentUser);
			      rfidAuth.required       = preferences.getBool(RFID_AUTH_REQUIRED_KEY, false);
			      doc["rfidAuthorized"]   = rfidAuth.authorized;
			      doc["rfidUserName"]     = currentUser.name;
			      doc["rfidAuthRequired"] = rfidAuth.required;
				      doc["chargeSessionAuthorized"] = chargeAuthSession.authorized;
				      doc["chargeSessionVehicleWasConnected"] = chargeAuthSession.vehicleWasConnected;
				      doc["chargeSessionAuthorizationMillis"] = chargeAuthSession.authorizationGrantedMillis;
			      doc["chargeSessionLastChargeMillis"] = chargeAuthSession.lastChargeActiveMillis;
			      doc["chargeSessionAuthorizationTime"] = formatSessionTime(chargeAuthSession.authorizationGrantedTime);
			      doc["chargeSessionLastChargeTime"] = formatSessionTime(chargeAuthSession.lastChargeActiveTime);
			      doc["chargeSessionIdTag"] = chargeAuthSession.idTag;
			      doc["chargeSessionUserName"] = chargeAuthSession.userName;
			      doc["chargeSessionMaxChargeMinutes"] = chargeAuthSession.maxChargeMinutes;
		      doc["rfidDb"]           = serialized(dbJson);
		      jsonRfid.reserve(1536);
	      serializeJson(doc, jsonRfid);
	    }

		    if (needSessions) {
		      JsonDocument doc;
		      deserializeJson(doc, charge_session_log_to_json_page(g_sessionPageOffset, SESSION_PAGE_LIMIT));
		      doc["wallboxName"] = preferences.getString("wallboxName", "InnoCharge");
		      session_mailer_append_status(doc.as<JsonObject>());
		      serializeJson(doc, jsonSessions);
		    }

        if (needApp) {
          bool cpDuty100 = (currentCpState.state == StateCustom_DutyCycle_100) || (getCpDuty >= 99.5f);
          JsonDocument doc;
          doc["wallboxName"] = preferences.getString("wallboxName", "InnoCharge");
          doc["cpState"] = cpStateToName(currentCpState.state);
          doc["phaseMode"] = currentCpState.threePhaseActive ? "Three-phase" : "Single-phase";
          doc["targetChargePower"] = cpDuty100 ? 0.0f : round(get_power_from_duty(getCpDuty)) / 10.0;
          doc["vehicleConnected"] = currentCpState.vehicleConnected;
          doc["chargingActive"] = currentCpState.chargingActive;
          doc["maxChargePower"] = digitalRead(DIP_SWITCH_1) == LOW ? 22 : 11;
          const LedVisualStatus led = getLedVisualStatus();
          JsonObject visual = doc["ledStatus"].to<JsonObject>();
          visual["label"] = led.label;
          char color[8];
          snprintf(color, sizeof(color), "#%06lx", static_cast<unsigned long>(led.color));
          visual["color"] = color;
          snprintf(color, sizeof(color), "#%06lx", static_cast<unsigned long>(led.waveColor));
          visual["waveColor"] = color;
          switch (led.animation) {
            case LedVisualAnimation::Wave: visual["animation"] = "wave"; break;
            case LedVisualAnimation::Charge: visual["animation"] = "charge"; break;
            case LedVisualAnimation::Blink: visual["animation"] = "blink"; break;
            default: visual["animation"] = "solid"; break;
          }
          visual["periodMs"] = led.periodMs;
          jsonApp.reserve(768);
          serializeJson(doc, jsonApp);
        }

			    if (needGridSettings) {
			      jsonGridSettings = build_grid_settings_json();
			    }

    // --- 4) Optional: nur bei Änderung senden (spart Last) ---
	    if (needIndex) lastIndex = jsonIndex;
	    if (needInterfaces && jsonInterfaces == lastInterfaces) needInterfaces = false; else lastInterfaces = jsonInterfaces;
		    if (needSystem     && jsonSystem     == lastSystem)     needSystem     = false; else lastSystem     = jsonSystem;
		    if (needRfid       && jsonRfid       == lastRfid)       needRfid       = false; else lastRfid       = jsonRfid;
		    if (needSessions   && jsonSessions   == lastSessions)   needSessions   = false; else lastSessions   = jsonSessions;
		    if (needGridSettings && jsonGridSettings == lastGridSettings) needGridSettings = false; else lastGridSettings = jsonGridSettings;

    // --- 5) Verteilen ---
    for (auto &c : clients) {
			      if      (c.second == "index"      && needIndex)      webSocket.sendTXT(c.first, jsonIndex);
		      else if (c.second == "app"        && needApp)        webSocket.sendTXT(c.first, jsonApp);
		      else if (c.second == "interfaces" && needInterfaces) webSocket.sendTXT(c.first, jsonInterfaces);
		      else if (c.second == "system"     && needSystem)     webSocket.sendTXT(c.first, jsonSystem);
		      else if (c.second == "rfid"       && needRfid)       webSocket.sendTXT(c.first, jsonRfid);
		      else if (c.second == "sessions"   && needSessions)   webSocket.sendTXT(c.first, jsonSessions);
		      else if (c.second == "grid_settings" && needGridSettings && grid_settings_client_unlocked(c.first)) webSocket.sendTXT(c.first, jsonGridSettings);
		    }

    vTaskDelay(pdMS_TO_TICKS(800));  // gleiche Rate wie zuvor
  }
}


void handleWifiScanRequest(AsyncWebServerRequest *request) {
    if (!web_request_has_session(request)) {
        request->send(403, "text/plain", "login required");
        return;
    }

    bool started = wifi_scan_start();
    JsonDocument doc;
    doc["scanning"] = wifi_scan_is_running();
    doc["ready"] = wifi_scan_has_result();
    doc["ok"] = started && wifi_scan_last_error()[0] == '\0';
    if (wifi_scan_last_error()[0] != '\0') {
        doc["error"] = wifi_scan_last_error();
    }
    JsonArray networks = doc["networks"].to<JsonArray>();
    if (wifi_scan_has_result()) {
        for (uint16_t i = 0; i < scanned_ap_count; ++i) {
            JsonObject network = networks.add<JsonObject>();
            network["name"] = scanned_aps[i].ssid;
            network["signal"] = scanned_aps[i].rssi;
            network["encryption"] = auth_mode_type(scanned_aps[i].authmode);
        }
    }
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleWifiScanStatusRequest(AsyncWebServerRequest *request) {
    if (!web_request_has_session(request)) {
        request->send(403, "text/plain", "login required");
        return;
    }

    JsonDocument doc;
    doc["scanning"] = wifi_scan_is_running();
    doc["ready"] = wifi_scan_has_result();
    doc["ok"] = wifi_scan_last_error()[0] == '\0';
    if (wifi_scan_last_error()[0] != '\0') {
        doc["error"] = wifi_scan_last_error();
    }

    JsonArray networks = doc["networks"].to<JsonArray>();
    if (wifi_scan_has_result()) {
        for (uint16_t i = 0; i < scanned_ap_count; ++i) {
            JsonObject network = networks.add<JsonObject>();
            network["name"] = scanned_aps[i].ssid;
            network["signal"] = scanned_aps[i].rssi;
            network["encryption"] = auth_mode_type(scanned_aps[i].authmode);
        }
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleSessionImportBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (index == 0) {
        if (!web_request_has_session(request)) {
            redirect_to_login(request);
            return;
        }

        SPIFFS.remove(SESSION_IMPORT_PATH);
        auto *upload = new SessionImportUpload();
        upload->file = SPIFFS.open(SESSION_IMPORT_PATH, FILE_WRITE);
        upload->failed = !upload->file;
        request->_tempObject = upload;
    }

    auto *upload = static_cast<SessionImportUpload*>(request->_tempObject);
    if (!upload) {
        request->send(500, "text/plain", "import buffer error");
        return;
    }

    if (!upload->failed && upload->file.write(data, len) != len) {
        upload->failed = true;
    }

    if (index + len == total) {
        if (upload->file) upload->file.close();
        bool ok = !upload->failed;
        delete upload;
        request->_tempObject = nullptr;
        if (ok) g_sessionImportPending = true;
        request->send(ok ? 202 : 500, "text/plain", ok ? "import queued" : "import upload failed");
    }
}

void A_Task_Web(void *pvParameter) {
    // Setup code
    ESP_LOGI(WEB_TAG, "Starting web task");
    www_password_storage = preferences.getString(WEB_PASSWORD_KEY, "admin");
    if (www_password_storage.length() == 0 || www_password_storage.length() > 32) {
        ESP_LOGW(WEB_TAG, "Invalid stored web password length. Falling back to default password.");
        www_password_storage = "admin";
        preferences.putString(WEB_PASSWORD_KEY, www_password_storage);
    }
    ESP_LOGI(WEB_TAG, "Web authentication ready. Rescue mode: %s", rescueMode ? "true" : "false");
    g_wsSubsMutex = xSemaphoreCreateMutex();
    if (!g_wsSubsMutex) {
        ESP_LOGE(WEB_TAG, "Failed to create websocket subscription mutex");
        vTaskDelete(nullptr);
    }

    server.on("/login.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/login.html", "text/html");
    });

    server.on("/app.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(SPIFFS, "/app.html", "text/html");
    });

    server.on("/recovery", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", RECOVERY_HTML);
    });

    server.on("/recovery.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", RECOVERY_HTML);
    });

    server.on("/login", HTTP_POST, [](AsyncWebServerRequest* request) {
        String username;
        String password;
        if (request->hasParam("username", true)) {
            username = request->getParam("username", true)->value();
        }
        if (request->hasParam("password", true)) {
            password = request->getParam("password", true)->value();
        }

        if (username != www_username || password != current_web_password()) {
            AsyncWebServerResponse* response = request->beginResponse(302);
            response->addHeader("Location", "/login.html");
            request->send(response);
            return;
        }

        String sessionToken = make_web_session_token();
        add_web_session(sessionToken);
        AsyncWebServerResponse* response = request->beginResponse(302);
        response->addHeader("Location", "/index.html");
        response->addHeader("Set-Cookie", String(WEB_SESSION_COOKIE) + "=" + sessionToken + "; Path=/; SameSite=Lax");
        request->send(response);
    });

    server.on("/logout", HTTP_GET, [](AsyncWebServerRequest* request) {
        remove_web_session(web_request_session_token(request));
        AsyncWebServerResponse* response = request->beginResponse(302);
        response->addHeader("Location", "/login.html");
        response->addHeader("Set-Cookie", String(WEB_SESSION_COOKIE) + "=; Path=/; Max-Age=0; SameSite=Lax");
        request->send(response);
    });

    register_protected_page_routes(server);

    server.serveStatic("/", SPIFFS, "/")
      .setDefaultFile("index.html");

    auto send204 = [&](AsyncWebServerRequest* req) {
    req->send(204);
    };

    // Wenn du (noch) KEIN Icon im SPIFFS hast:
    server.on("/favicon.ico", HTTP_ANY, send204);
    // Typische weitere automatische Requests:
    server.on("/apple-touch-icon.png", HTTP_ANY, send204);
    server.on("/site.webmanifest",     HTTP_ANY, send204);
    server.on("/robots.txt",           HTTP_ANY, send204);

    server.onNotFound([&](AsyncWebServerRequest* req){
    redirect_to_login(req);
    });

    server.onRequestBody([](AsyncWebServerRequest*, uint8_t*, size_t, size_t, size_t) {});

    setupUploadMain();
    setupUploadUi();

    // Register routes
    registerWebRoutes(server);
    server.on("/wifi_scan", HTTP_GET, handleWifiScanRequest);
    server.on("/wifi_scan_status", HTTP_GET, handleWifiScanStatusRequest);
    server.on("/importsessions", HTTP_POST, [](AsyncWebServerRequest*) {}, nullptr, handleSessionImportBody);

    server.begin(); // start server
    ESP_LOGI(WEB_TAG, "HTTP server started on port 80");

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    ESP_LOGI(WEB_TAG, "WebSocket server started on port 81");
    //start_periodic_timer();
    xTaskCreatePinnedToCore(Task_WebPush, "WebPush", 4096, nullptr, 3, nullptr, 1);

	    while(1) {
	        webSocket.loop();
	        if (g_sessionImportPending) {
	            g_sessionImportPending = false;
	            bool ok = charge_session_log_import_file(SESSION_IMPORT_PATH);
	            SPIFFS.remove(SESSION_IMPORT_PATH);
	            ESP_LOGI(WEB_TAG, "Session import %s", ok ? "ok" : "failed");
	        }
	        if (g_rebootRequested) {
	            vTaskDelay(500 / portTICK_PERIOD_MS);
	            esp_restart();
	        }
	        vTaskDelay(50 / portTICK_PERIOD_MS);
	    }
}
