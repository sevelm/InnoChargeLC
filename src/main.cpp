#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "SPIFFS.h"
#include "ESPAsyncWebServer.h" // needed to create a simple webserver (make sure tools -> board is set to ESP32, otherwise you will get a "WebServer.h: No such file or directory" error)
#include "AsyncTCP.h"
#include "WebSocketsServer.h"  // needed for instant communication between client and server through Websockets
#include "ArduinoJson.h"       // needed for JSON encapsulation (send multiple variables with one string)

#include "ethernet_manager.hpp"
#include "lock_ctrl.hpp"
#include "A_Task_Low.hpp"
#include "A_Task_Web.hpp"
#include "ledEffect.hpp"
#include "AA_globals.h"
#include "A_Task_CP.hpp"
#include "demo_codes.hpp"


const char *MAIN_TAG = "Main: ";


// Initialization of webserver and websocket
AsyncWebServer server(80);                            // the server uses port 80 (standard port for websites
class CaptiveRequestHandler : public AsyncWebHandler {
public:
    CaptiveRequestHandler() {}
    virtual ~CaptiveRequestHandler() {}

    bool canHandle(AsyncWebServerRequest *request) {
        // This handler will handle all requests
        return true;
    }

    void handleRequest(AsyncWebServerRequest *request) {
        File file = SPIFFS.open("/index.html", "r");
        if (!file) {
            // If the file cannot be opened, send a default response
            AsyncResponseStream *response = request->beginResponseStream("text/html");
            response->print("<!DOCTYPE html><html><head><title>Captive Portal</title></head><body>");
            response->print("<p>Failed to open file for reading.</p>");
            response->printf("<p>You were trying to reach: http://%s%s</p>", request->host().c_str(), request->url().c_str());
            response->printf("<p>Try opening <a href='http://%s'>this link</a> instead</p>", WiFi.softAPIP().toString().c_str());
            response->print("</body></html>");
            request->send(response);
        } else {
            // If the file is opened successfully, send its content
            AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/index.html", "text/html");
            request->send(response);
            file.close();
        }
    }
};




//Globals

//cp_measurements_t measurements = {0.0, 0.0, {0.0}, 0};
NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(8, 10);
int cpState;
float highVoltage;
charging_state_t currentCpState = StateA_NotConnected;


//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////

//###################### HTTP Event Handler


void setup() {
    ESP_LOGI(MAIN_TAG, "Starting up EVSE Test Programm!");

    //######################### Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    //######################### Initialize TCP/IP Stack and Default Event Loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(MAIN_TAG, "Starting Ethernet...");

    // Just this is enough to start Ethernet in DHCP mode
    // start_eth(true, NULL); // Start Ethernet with DHCP Client enabled

    // Start Ethernet in Static Mode
    ethernet_start_config_t eth_config = {
        .mac_addr = {0x3C, 0x71, 0xBF, 0x0A, 0x0B, 0x0C},
        .ip_addr = {192, 168, 1, 156},
        .netmask = {255, 255, 255, 0},
        .gw = {192, 168, 1, 254},
        .dns1 = {8, 8, 8, 8},
        .dns2 = {8, 8, 4, 4}
    };
    start_eth(false, &eth_config); // Start Ethernet with DHCP Client disabled
    ESP_LOGI(MAIN_TAG, "Ethernet started.");

    //######################### LED-Pixles 
    strip.Begin();              // INITIALIZE NeoPixel strip object (REQUIRED)
    strip.Show();               // Initialize all strip to 'off'
    for (int i = 0; i < 8; i++) {
        strip.SetPixelColor(i, RgbColor(0, 0, 255)); 
    }
    strip.Show();   // Send the updated pixel colors to the hardware.

    //######################### Webserver Start
    // Initialize SPIFFS
    if(!SPIFFS.begin(true)){
        ESP_LOGI(MAIN_TAG, "An Error has occurred while mounting SPIFFS");
        return;
    }
    server.addHandler(new CaptiveRequestHandler());

    //###################### Allgemein-Seite via SPIFFS  
      // Login Seite Laden
      server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
        request->send(SPIFFS, "/index.html", "text/html");
      });
      // Index Seite Laden
      server.on("/index", HTTP_GET, [](AsyncWebServerRequest * request) {
        request->send(SPIFFS, "/index.html", "text/html");
      });

    server.begin();                     // start server -> best practice is to start the server after the websocket
    server.serveStatic("/", SPIFFS, "/");
    //webSocket.begin();                  // start websocket
    //webSocket.onEvent(webSocketEvent);  // define a callback function -> what does the ESP32 need to do when an event from the websocket is received? -> run function "webSocketEvent()"

    //######################### Create Task and Start
    // xTaskCreatePinnedToCore(A_Task_CP, "Controlpilot Task", 8192, NULL, 1, NULL, 1);
    // xTaskCreatePinnedToCore(A_Task_Web, "Task_Web_Operation", 8192, NULL, 4, NULL, 1);
    // xTaskCreatePinnedToCore(A_Task_Low, "Task_Low_Operation", 8192, NULL, 5, NULL, 1);
    xTaskCreate(demo_monitoring_task, "Demo Monitoring Task", 4096, NULL, 1, NULL);

    // xTaskCreate(pp_monitoring_task, "PP Monitoring Task", 4096, NULL, 1, NULL);
    // xTaskCreate(lock_monitor_task, "Lock Monitor Task", 2048, NULL, 5, NULL);
    // xTaskCreate(relay_ctrl_test_task, "Relay Control Test Task", 2048, NULL, 5, NULL);
}


//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
void loop() {
    // control LED
    callLedEffect();
    
    // lock_lock();
    // vTaskDelay(5000 / portTICK_PERIOD_MS);
    // release_lock();
    // vTaskDelay(5000 / portTICK_PERIOD_MS);
}

