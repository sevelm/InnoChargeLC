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
#include "ethernet_manager.hpp"
#include "wifi_manager.hpp"
#include "esp_wifi.h"

#include "esp_timer.h"
#include <map>

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

OtaStatus otaMain{};
OtaStatus otaUi{};

SemaphoreHandle_t g_wsSubsMutex;   // schützt subscribedClients
static std::vector<uint8_t> g_pendingNetworkOnce;

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
                    } else if (strcmp(action, "setEnergySign") == 0 && doc["state"].is<bool>()) {
                        bool state = doc["state"].as<bool>();
                        preferences.putBool("emSignEnable", state); 
                        sdm.invSign = state;    
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


static void Task_WebPush(void* arg) {
  // Caches der letzten JSONs je Seite (um identische Frames nicht zu spammen)
  String lastIndex, lastInterfaces, lastSystem;

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

        StaticJsonDocument<640> doc;
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
    bool needIndex = false, needInterfaces = false, needSystem = false;
    for (auto &c : clients) {
      if      (c.second == "index")      needIndex      = true;
      else if (c.second == "interfaces") needInterfaces = true;
      else if (c.second == "system")     needSystem     = true;
    }

    // --- 3) JSONs je Seite GENAU EINMAL bauen (nur wenn benötigt) ---
    String jsonIndex, jsonInterfaces, jsonSystem;

    if (needIndex) {
      StaticJsonDocument<384> doc;
      doc["cpState"]             = cpStateToName(currentCpState.state);
      doc["cpVoltage"]           = round(highVoltage * 10) / 10.0;
      doc["espTemp"]             = round(readEspTemperatureC() * 10) / 10.0;
      doc["phaseMode"] = currentCpState.threePhaseActive ? "Three-phase" : "Single-phase";
      doc["targetChargeCurrent"] = (int)round(get_current_from_duty(getCpDuty));
      doc["targetChargePower"]   = round(get_power_from_duty(getCpDuty)) / 10.0;
      doc["cpRelayState"]        = get_cp_relays_status();
      jsonIndex.reserve(256);
      serializeJson(doc, jsonIndex);
    }

    if (needInterfaces) {
      StaticJsonDocument<512> doc;
      doc["energyMeterState"] = preferences.getBool("emEnable", false);
      doc["energySignState"]  = preferences.getBool("emSignEnable", false);
      doc["l1Voltage"] =  (int16_t)roundf(sdm.voltL1 * 10);
      doc["l2Voltage"] =  (int16_t)roundf(sdm.voltL2 * 10);
      doc["l3Voltage"] =  (int16_t)roundf(sdm.voltL3 * 10);
      doc["l1Current"] =  (int16_t)roundf(sdm.currL1 * 10);
      doc["l2Current"] =  (int16_t)roundf(sdm.currL2 * 10);
      doc["l3Current"] =  (int16_t)roundf(sdm.currL3 * 10);
      doc["l1Power"]   =  (int16_t)roundf(sdm.pwrL1  * 10);
      doc["l2Power"]   =  (int16_t)roundf(sdm.pwrL2  * 10);
      doc["l3Power"]   =  (int16_t)roundf(sdm.pwrL3  * 10);
      doc["totPower"]  =  (int16_t)roundf(sdm.pwrTot * 10);
      doc["impPower"]  =  (int16_t)roundf(sdm.enrImp * 100);
      doc["expPower"]  =  (int16_t)roundf(sdm.enrExp * 100);
      doc["energyMeterError"] = sdm.error;
      doc["rfidState"]        = preferences.getBool("rfidEnable", false);
      doc["rfidTag"]          = rfid.uidStr;
      doc["lastRfidTag"]      = rfid.lastUidStr;
      doc["rfidError"]        = rfid.error;
      jsonInterfaces.reserve(384);
      serializeJson(doc, jsonInterfaces);
    }

    if (needSystem) {
      StaticJsonDocument<256> doc;
      doc["otaMainProgress"] = otaMain.progress;
      doc["otaMainCode"]     = otaMain.code;
      doc["otaMainMessage"]  = otaMain.message;
      doc["otaMainVersion"]  = FW_VERSION_MAIN;
      doc["otaUiProgress"]   = otaUi.progress;
      doc["otaUiCode"]       = otaUi.code;
      doc["otaUiMessage"]    = otaUi.message;
      jsonSystem.reserve(256);
      serializeJson(doc, jsonSystem);
    }

    // --- 4) Optional: nur bei Änderung senden (spart Last) ---
    if (needIndex      && jsonIndex      == lastIndex)      needIndex      = false; else lastIndex      = jsonIndex;
    if (needInterfaces && jsonInterfaces == lastInterfaces) needInterfaces = false; else lastInterfaces = jsonInterfaces;
    if (needSystem     && jsonSystem     == lastSystem)     needSystem     = false; else lastSystem     = jsonSystem;

    // --- 5) Verteilen ---
    for (auto &c : clients) {
      if      (c.second == "index"      && needIndex)      webSocket.sendTXT(c.first, jsonIndex);
      else if (c.second == "interfaces" && needInterfaces) webSocket.sendTXT(c.first, jsonInterfaces);
      else if (c.second == "system"     && needSystem)     webSocket.sendTXT(c.first, jsonSystem);
    }

    vTaskDelay(pdMS_TO_TICKS(800));  // gleiche Rate wie zuvor
  }
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

void A_Task_Web(void *pvParameter) {
    // Setup code
    server.serveStatic("/", SPIFFS, "/")
      .setDefaultFile("index.html")
      .setAuthentication(www_username, www_password);

    auto send204 = [&](AsyncWebServerRequest* req) {
    if (!req->authenticate(www_username, www_password))
        return req->requestAuthentication();
    req->send(204);
    };

    // Wenn du (noch) KEIN Icon im SPIFFS hast:
    server.on("/favicon.ico", HTTP_ANY, send204);
    // Typische weitere automatische Requests:
    server.on("/apple-touch-icon.png", HTTP_ANY, send204);
    server.on("/site.webmanifest",     HTTP_ANY, send204);
    server.on("/robots.txt",           HTTP_ANY, send204);

    server.onNotFound([&](AsyncWebServerRequest* req){
    if (!req->authenticate(www_username, www_password))
        return req->requestAuthentication();
    req->send(SPIFFS, "/index.html", "text/html");
    });
    
    server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        ESP_LOGI(WEB_TAG, "Body: %s", (char *)data);
    });

    setupUploadMain();
    setupUploadUi();

    // Register routes
    registerWebRoutes(server);
    server.on("/wifi_scan", HTTP_GET, handleWifiScanRequest);

    server.begin(); // start server

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    //start_periodic_timer();
    g_wsSubsMutex = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(Task_WebPush, "WebPush", 4096, nullptr, 3, nullptr, 1);

    while(1) {
        webSocket.loop();
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}
