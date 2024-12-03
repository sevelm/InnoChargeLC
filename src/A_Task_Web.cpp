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

#include "AA_globals.h"
#include "A_Task_CP.hpp"
#include "control_pilot.hpp"
#include "ethernet_manager.hpp"
#include "wifi_manager.hpp"

#include "esp_timer.h"
#include <map>


esp_timer_handle_t periodic_timer;      // periotic JSON send
//std::set<uint8_t> subscribedClients;    // Client number
std::map<uint8_t, std::string> subscribedClients;    // Client number with page

const char *WEB_TAG = "Task_Web: ";

// Initialization of webserver and websocket
AsyncWebServer server(80); // the server uses port 80 (standard port for websites

class CaptiveRequestHandler : public AsyncWebHandler {
public:
    CaptiveRequestHandler() {}
    virtual ~CaptiveRequestHandler() {}

    bool canHandle(AsyncWebServerRequest *request) {
        // This handler will handle all requests that are not found in SPIFFS
        return !SPIFFS.exists(request->url());
    }

    void handleRequest(AsyncWebServerRequest *request) {
        ESP_LOGI(WEB_TAG, "Handling request for %s", request->url().c_str());
        File file = SPIFFS.open("/index.html", "r");
        if (!file) {
            // If the file cannot be opened, send a default response
            ESP_LOGE(WEB_TAG, "Failed to open /index.html");
            AsyncResponseStream *response = request->beginResponseStream("text/html");
            response->print("<!DOCTYPE html><html><head><title>Captive Portal</title></head><body>");
            response->print("<p>Failed to open file for reading.</p>");
            response->printf("<p>You were trying to reach: http://%s%s</p>", request->host().c_str(), request->url().c_str());
            response->printf("<p>Try opening <a href='http://%s'>this link</a> instead</p>", WiFi.softAPIP().toString().c_str());
            response->print("</body></html>");
            request->send(response);
        } else {
            // If the file is opened successfully, send its content
            ESP_LOGI(WEB_TAG, "Serving /index.html");
            AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/index.html", "text/html");
            request->send(response);
            file.close();
        }
    }
};

WebSocketsServer webSocket = WebSocketsServer(81);

//volatile bool clientConnected = false;

void webSocketEvent(byte num, WStype_t type, uint8_t * payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            subscribedClients.erase(num);
            ESP_LOGI(WEB_TAG, "Client %s disconnected", String(num).c_str());
            // clientConnected = false;
            break;
        case WStype_CONNECTED:
            ESP_LOGI(WEB_TAG, "Client %s connected", String(num).c_str());
            // clientConnected = true;
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
                        ESP_LOGI(WEB_TAG, "Client %u subscribed to updates", num);
                    } else if (strcmp(action, "unsubscribeUpdates") == 0) {
                        subscribedClients.erase(num);
                        ESP_LOGI(WEB_TAG, "Client %u unsubscribed from updates", num);
                    }
                    // Handle action for resetting TCP before applying new network settings
                   // if (strcmp(action, "resetTCP") == 0) {
                   //     ESP_LOGI(WEB_TAG, "Resetting TCP connections before applying new network settings");
                   //     stop_eth();  // Stop Ethernet and close TCP connections
                   //     break;  // After resetting, wait for new settings to be sent
                   // }
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

              //   String jsonString;
              //   serializeJsonPretty(doc, jsonString);  // Speichert das JSON im String jsonString
              //   ESP_LOGI(WEB_TAG, "Received JSON data:\n%s", jsonString.c_str());
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
                doc["cpRelayState"] = get_cp_relays_status();
             }
        serializeJson(doc, jsonString);
        webSocket.sendTXT(clientNum, jsonString);
        //ESP_LOGI(WEB_TAG, "Sending JSON: %s", jsonString.c_str());
        
        
        
     //   for (auto clientNum : subscribedClients) {
     //       webSocket.sendTXT(clientNum, jsonString);
      //  }
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



/*void webSocketCreate(void *pvParameter) {
    while(1) {
        if (clientConnected) {
            String jsonString = "";
            JsonDocument doc;
            JsonObject object = doc.to<JsonObject>();
           // object["ethStaticState"]  = preferences.getBool("ethStatic", false);
            object["cpState"]       = cpStateToName(currentCpState);
            object["cpRelayState"]  = get_cp_relays_status();
            serializeJson(doc, jsonString);
            webSocket.broadcastTXT(jsonString);
        }
        vTaskDelay(800 / portTICK_PERIOD_MS);
    }
}*/

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
    // Führe den WiFi-Scan immer durch, um aktuelle Ergebnisse zu erhalten
   
    wifi_scan();

    // Passe die Größe des JSON-Dokuments an
    DynamicJsonDocument doc(4096);
    JsonArray networks = doc.createNestedArray("networks");

    for (uint16_t i = 0; i < scanned_ap_count; ++i) {
        JsonObject network = networks.createNestedObject();
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
    server.addHandler(new CaptiveRequestHandler()).setFilter(ON_STA_FILTER); // Only handle requests from STA
    server.onNotFound([](AsyncWebServerRequest *request) {
        ESP_LOGI(WEB_TAG, "Not found: %s", request->url().c_str());
        request->send(404, "text/plain", "Not Found");
    });

    server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        ESP_LOGI(WEB_TAG, "Body: %s", (char *)data);
    });

    // Register routes
    registerWebRoutes(server);
    server.on("/get_EthNetwork_info", HTTP_GET, handleEthNetworkRequest);
    server.on("/get_WifiNetwork_info", HTTP_GET, handleWifiNetworkRequest);
    server.on("/wifi_scan", HTTP_GET, handleWifiScanRequest);

    server.begin(); // start server

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    start_periodic_timer();
   // xTaskCreate(webSocketCreate, "WebSocketCreateTask", 2048, NULL, 1, NULL);

    while(1) {
        webSocket.loop();
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}
