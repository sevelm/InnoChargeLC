// A_Task_web.cpp

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
#include <Update.h>
#include "esp_ota_ops.h"
#include "AA_globals.h"
#include "A_Task_CP.hpp"
#include "control_pilot.hpp"
#include "ethernet_manager.hpp"
#include "wifi_manager.hpp"

#include "esp_timer.h"
#include <map>

#include <esp_ota_ops.h>        // ↯ einmal ganz oben in A_Task_Web.cpp


#ifndef FW_VERSION_MAIN
#define FW_VERSION_MAIN "V.UNKNOWN"
#endif
#ifndef FW_VERSION_UI
#define FW_VERSION_UI "V.UNKNOWN"
#endif


/* ---------- OTA-MAIN-Status (global) ---------- */
struct {
    volatile uint8_t progress = 0;   // 0-100
    volatile int8_t  code     =  0;  // 0=idle, 1=ok, <0 = Fehler
    String  message;                 // Mensch-lesbarer Text
} otaMain;
/* ---------- OTA-UI-Status (global) ---------- */
struct {
    volatile uint8_t progress = 0;   // 0-100
    volatile int8_t  code     =  0;  // 0=idle, 1=ok, <0 = Fehler
    String  message;                 // Mensch-lesbarer Text
} otaUi;



esp_timer_handle_t periodic_timer;      // periotic JSON send
std::map<uint8_t, std::string> subscribedClients;    // Client number with page

const char *WEB_TAG = "Task_Web: ";

// Initialization of webserver and websocket
AsyncWebServer server(80); // the server uses port 80 (standard port for websites

const char* www_username = "admin";
const char* www_password = "admin";

class CaptiveRequestHandler : public AsyncWebHandler {
public:
    CaptiveRequestHandler() {}
    virtual ~CaptiveRequestHandler() {}

    bool canHandle(AsyncWebServerRequest *request) {
        // This handler will handle all requests that are not found in SPIFFS
        return !SPIFFS.exists(request->url());
    }

    void handleRequest(AsyncWebServerRequest *request) {
        if (!request->authenticate(www_username, www_password)) {
            return request->requestAuthentication(); // fordert Benutzer+Passwort an
        }
        //ESP_LOGI(WEB_TAG, "Handling request for %s", request->url().c_str());
        File file = SPIFFS.open("/index.html", "r");
        if (!file) {
            // If the file cannot be opened, send a default response
        //    ESP_LOGE(WEB_TAG, "Failed to open /index.html");
            AsyncResponseStream *response = request->beginResponseStream("text/html");
            response->print("<!DOCTYPE html><html><head><title>Captive Portal</title></head><body>");
            response->print("<p>Failed to open file for reading.</p>");
            response->printf("<p>You were trying to reach: http://%s%s</p>", request->host().c_str(), request->url().c_str());
            response->printf("<p>Try opening <a href='http://%s'>this link</a> instead</p>", WiFi.softAPIP().toString().c_str());
            response->print("</body></html>");
            request->send(response);
        } else {
            // If the file is opened successfully, send its content
         //   ESP_LOGI(WEB_TAG, "Serving /index.html");
            AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/index.html", "text/html");
            request->send(response);
            file.close();
        }
    }
};

WebSocketsServer webSocket = WebSocketsServer(81);

void webSocketEvent(byte num, WStype_t type, uint8_t * payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            subscribedClients.erase(num);
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
                    if (strcmp(action, "subscribeUpdates") == 0) {
                        const char* page = doc["page"];
                        subscribedClients[num] = std::string(page);
                  //      ESP_LOGI(WEB_TAG, "Client %u subscribed to updates", num);
                    } else if (strcmp(action, "unsubscribeUpdates") == 0) {
                        subscribedClients.erase(num);
                  //      ESP_LOGI(WEB_TAG, "Client %u unsubscribed from updates", num);
                    } else if (strcmp(action, "setCpRelayState") == 0 && doc["state"].is<bool>()) {
                        bool state = doc["state"].as<bool>();
                        state ? turn_on_cp_relay() : turn_off_cp_relay();
                    } else if (strcmp(action, "setChargeParameters") == 0 && doc.containsKey("current")) {
                        int current = doc["current"].as<int>();
                        set_charging_current(current);
                    } else if (strcmp(action, "setChargeParameters") == 0 && doc.containsKey("power")) {
                        float power = doc["power"].as<float>();
                        set_charging_power(power*10);
                        // Page-Interfaces
                    } else if (strcmp(action, "setEnergyMeter") == 0 && doc["state"].is<bool>()) {
                        bool state = doc["state"].as<bool>();
                        preferences.putBool("emEnable", state);  
                        sdm.enable = state;
                      //  sdm.error = state;  
                    } else if (strcmp(action, "setRfid") == 0 && doc["state"].is<bool>()) {
                        bool state = doc["state"].as<bool>();
                        preferences.putBool("rfidEnable", state); 
                        rfid.enable = state;
                       // rfid.error = state;
                       // ESP_LOGI(WEB_TAG, "energyMeterEnable = %d", preferences.getBool("rfidEnable", false));
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



void periodic_timer_callback(void* arg) {
    if (!subscribedClients.empty()) {
        for (const auto& clientPair : subscribedClients) {
            uint8_t clientNum = clientPair.first;
            const std::string& page = clientPair.second;
            String jsonString;
            JsonDocument doc;
             if (page == "index") {
                doc["cpState"] = cpStateToName(currentCpState);
                doc["cpVoltage"] = round(highVoltage * 10) / 10.0;
                doc["targetChargeCurrent"] = round(get_current_from_duty(getCpDuty));
                doc["targetChargePower"] = round(get_power_from_duty(getCpDuty)) / 10.0;                
              // doc["targetChargePower"] = (int)(get_power_from_duty(duty) / 10 * 10 + 0.5) / 10.0;
                doc["cpRelayState"] = get_cp_relays_status();
             }
             if (page == "interfaces") {
                // Energymeter
                doc["energyMeterState"] = preferences.getBool("emEnable", false);
                doc["l1Voltage"] =  (int16_t)roundf(sdm.voltL1 * 10);
                doc["l2Voltage"] =  (int16_t)roundf(sdm.voltL2 * 10);
                doc["l3Voltage"] =  (int16_t)roundf(sdm.voltL3 * 10);
                doc["l1Current"] =  (int16_t)roundf(sdm.currL1 * 10);
                doc["l2Current"] =  (int16_t)roundf(sdm.currL2 * 10);
                doc["l3Current"] =  (int16_t)roundf(sdm.currL3 * 10);
                doc["l1Power"] =  (int16_t)roundf(sdm.pwrL1 * 10);
                doc["l2Power"] =  (int16_t)roundf(sdm.pwrL2 * 10);
                doc["l3Power"] =  (int16_t)roundf(sdm.pwrL3 * 10);
                doc["totPower"] =  (int16_t)roundf(sdm.pwrTot * 10);
                doc["impPower"] =  (int16_t)roundf(sdm.enrImp * 100);
                doc["expPower"] =  (int16_t)roundf(sdm.enrExp * 100);
                doc["energyMeterError"] =  sdm.error;
                // RFID
                doc["rfidState"]        = preferences.getBool("rfidEnable", false);
                doc["rfidTag"] =  rfid.uidStr;
                doc["lastRfidTag"] =  rfid.lastUidStr;
                doc["rfidError"] =  rfid.error;
             }
             if (page == "system") {
                // System
                doc["otaMainProgress"] = otaMain.progress;      // 0-100%
                doc["otaMainCode"]     = otaMain.code;          // 1=ok,0=busy,<0 err
                doc["otaMainMessage"]  = otaMain.message;       // Messages
                doc["otaMainVersion"]  = FW_VERSION_MAIN;       // Version aus txt
                doc["otaUiProgress"] = otaUi.progress;          // 0-100%
                doc["otaUiCode"]     = otaUi.code;              // 1=ok,0=busy,<0 err
                doc["otaUiMessage"]  = otaUi.message;           // Messages
                doc["otaUiVersion"]  = FW_VERSION_UI;              // Version aus txt
             }
        serializeJson(doc, jsonString);
        webSocket.sendTXT(clientNum, jsonString);
        //ESP_LOGI(WEB_TAG, "Sending JSON: %s", jsonString.c_str());
        }
    }
}
void start_periodic_timer() {
    const esp_timer_create_args_t periodic_timer_args = {
            .callback = &periodic_timer_callback,
            .name = "periodic_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    // start timer 800 ms intervall
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 800000)); // 800000 µs = 800 ms
}


void handleEthNetworkRequest(AsyncWebServerRequest *request) {
    ethernet_state_t eth_status;
    get_ethernet_state(&eth_status);
    char macStr[18];
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", eth_status.mac_addr[0], eth_status.mac_addr[1], eth_status.mac_addr[2], eth_status.mac_addr[3], eth_status.mac_addr[4], eth_status.mac_addr[5]);
    
    // Add the other values to the response
    String response = "{";
    // Ethernet
    response += "\"eth_mac\":\"" + String(macStr) + "\",";
    response += "\"eth_ip\":\"" + String(eth_status.ip_addr[0]) + "." + String(eth_status.ip_addr[1]) + "." + String(eth_status.ip_addr[2]) + "." + String(eth_status.ip_addr[3]) + "\",";
    response += "\"eth_netmask\":\"" + String(eth_status.netmask[0]) + "." + String(eth_status.netmask[1]) + "." + String(eth_status.netmask[2]) + "." + String(eth_status.netmask[3]) + "\",";
    response += "\"eth_gateway\":\"" + String(eth_status.gw[0]) + "." + String(eth_status.gw[1]) + "." + String(eth_status.gw[2]) + "." + String(eth_status.gw[3]) + "\",";
    response += "\"eth_dns1\":\"" + String(eth_status.dns1[0]) + "." + String(eth_status.dns1[1]) + "." + String(eth_status.dns1[2]) + "." + String(eth_status.dns1[3]) + "\",";
    response += "\"eth_dns2\":\"" + String(eth_status.dns2[0]) + "." + String(eth_status.dns2[1]) + "." + String(eth_status.dns2[2]) + "." + String(eth_status.dns2[3]) + "\",";
    response += "\"eth_static\":" + String(preferences.getBool("ethStatic", false) ? "true" : "false") + ",";
    // Wireless
    response += "\"wifi_enable\":" + String(preferences.getBool("wifiEnable", false) ? "true" : "false");

    response += "}";
    request->send(200, "application/json", response);
}

void handleWifiNetworkRequest(AsyncWebServerRequest *request) {
    if (!preferences.getBool("wifiEnable", false)) {
        request->send(200, "application/json",
                      "{\"wifi_enable\":false}");
        return;
    }
    wifi_sta_state_t wifi_status;
    get_wifi_sta_state(&wifi_status);
    char macStr[18];
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", wifi_status.mac[0], wifi_status.mac[1], wifi_status.mac[2], wifi_status.mac[3], wifi_status.mac[4], wifi_status.mac[5]);

    // Add the other values to the response
    String response = "{";
    response += "\"wifi_mac\":\"" + String(macStr) + "\",";
    response += "\"wifi_ssid\":\"" + String((char*)wifi_status.ssid) + "\",";
    response += "\"wifi_ip\":\"" + String(wifi_status.ip[0]) + "." + String(wifi_status.ip[1]) + "." + String(wifi_status.ip[2]) + "." + String(wifi_status.ip[3]) + "\",";
    response += "\"wifi_netmask\":\"" + String(wifi_status.netmask[0]) + "." + String(wifi_status.netmask[1]) + "." + String(wifi_status.netmask[2]) + "." + String(wifi_status.netmask[3]) + "\",";
    response += "\"wifi_gateway\":\"" + String(wifi_status.gateway[0]) + "." + String(wifi_status.gateway[1]) + "." + String(wifi_status.gateway[2]) + "." + String(wifi_status.gateway[3]) + "\",";
    response += "\"wifi_dns1\":\"" + String(wifi_status.dns1[0]) + "." + String(wifi_status.dns1[1]) + "." + String(wifi_status.dns1[2]) + "." + String(wifi_status.dns1[3]) + "\",";
    response += "\"wifi_dns2\":\"" + String(wifi_status.dns2[0]) + "." + String(wifi_status.dns2[1]) + "." + String(wifi_status.dns2[2]) + "." + String(wifi_status.dns2[3]) + "\",";
    response += "\"wifi_static\":" + String(preferences.getBool("wifiStatic", false) ? "true" : "false") + ",";
    response += "\"wifi_connected\":" + String(wifi_status.connected ? "true" : "false") + ",";
    response += "\"wifi_enable\":" + String(preferences.getBool("wifiEnable", false) ? "true" : "false") + ",";
    response += "\"wifi_pwd\":\"" + String((char*)wifi_status.passphrase) + "\"";
    response += "}";
    // Send answer
    request->send(200, "application/json", response);
}


void handleWifiScanRequest(AsyncWebServerRequest *request) {
    wifi_scan();
    JsonDocument doc;
    JsonArray networks = doc["networks"].to<JsonArray>();
    for (uint16_t i = 0; i < scanned_ap_count; ++i) {
        JsonObject network = networks.add<JsonObject>();
        network["name"] = scanned_aps[i].ssid;
        network["signal"] = scanned_aps[i].rssi;
        network["encryption"] = auth_mode_type(scanned_aps[i].authmode);
    }
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

// * =====================================================
// * OTA-UPLOAD-HANDLER  (neu)
// * -----------------------------------------------------


/* ---------- Upload‑Handler MAIN ---------- */
static void setupUploadMain()
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
static void setupUploadUi()
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







void A_Task_Web(void *pvParameter) {
    // Setup code
    server.serveStatic("/", SPIFFS, "/").setAuthentication(www_username, www_password);
    server.addHandler(new CaptiveRequestHandler()).setFilter(ON_STA_FILTER); // Only handle requests from STA
    server.onNotFound([](AsyncWebServerRequest *request) {
    //    ESP_LOGI(WEB_TAG, "Not found: %s", request->url().c_str());
        request->send(404, "text/plain", "Not Found");
    });

    server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        ESP_LOGI(WEB_TAG, "Body: %s", (char *)data);
    });


    setupUploadMain();
    setupUploadUi();

    // Register routes
    registerWebRoutes(server);
    server.on("/get_EthNetwork_info", HTTP_GET, handleEthNetworkRequest);
    server.on("/get_WifiNetwork_info", HTTP_GET, handleWifiNetworkRequest);
    server.on("/wifi_scan", HTTP_GET, handleWifiScanRequest);

    server.begin(); // start server

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    start_periodic_timer();

    while(1) {
        webSocket.loop();
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}
