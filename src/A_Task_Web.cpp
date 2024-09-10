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

volatile bool clientConnected = false;

void webSocketEvent(byte num, WStype_t type, uint8_t * payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            ESP_LOGI(WEB_TAG, "Client %s disconnected", String(num).c_str());
            clientConnected = false;
            break;
        case WStype_CONNECTED:
            ESP_LOGI(WEB_TAG, "Client %s connected", String(num).c_str());
            clientConnected = true;
            break;
        case WStype_TEXT:
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);
            if (error) {
                return;
            } else {
                JsonVariantConst val;

                // Handle action for resetting TCP before applying new network settings
                if (doc.containsKey("action") && doc["action"] == "resetTCP") {
                    ESP_LOGI(WEB_TAG, "Resetting TCP connections before applying new network settings");
                    stop_eth();  // Stop Ethernet and close TCP connections
                    break;  // After resetting, wait for new settings to be sent
                }

                // Safely handle array conversion for each setting
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

                // Set the new Ethernet mode (static or dynamic) and restart Ethernet
                val = doc["setEthStatic"];
                if (val != nullptr) {
                    preferences.putBool("ethStatic", val.as<bool>());
                    // After preferences are set, restart Ethernet
                    stop_eth();
                    restart_new_settings_eth();
                }

                val = doc["setWifiEnable"];
                if (val != nullptr) {
                    preferences.putBool("wifiEnable", val.as<bool>());
                    // Add handling if necessary for WiFi here
                }

                serializeJsonPretty(doc, Serial);  // Output the received settings for debugging
            }
            break;
    }
}


void webSocketCreate(void *pvParameter) {
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
}

void handleNetworkRequest(AsyncWebServerRequest *request) {
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
    server.on("/get_network_info", HTTP_GET, handleNetworkRequest);

    server.begin(); // start server

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

    xTaskCreate(webSocketCreate, "WebSocketCreateTask", 2048, NULL, 1, NULL);

    while(1) {
        webSocket.loop();
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}
