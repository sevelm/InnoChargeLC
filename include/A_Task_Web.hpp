// A_Task_Web.hpp
#pragma once
#include "ESPAsyncWebServer.h"
#include "SPIFFS.h"

#include <Arduino.h>
#include <map>

class AsyncWebServer;
class WebSocketsServer;

// OTA-Status (entspricht deinen otaMain/otaUi Feldern)
struct OtaStatus {
  volatile uint8_t progress = 0;  // 0-100
  volatile int8_t  code     = 0;  // 0=idle, 1=ok, <0 Fehler
  String           message;
};

// Globals, die in A_Task_Web.cpp DEFINIERT sind
extern AsyncWebServer server;
extern WebSocketsServer webSocket;
extern std::map<uint8_t, std::string> subscribedClients;
extern OtaStatus otaMain;
extern OtaStatus otaUi;

// Routen-Registrierung (nur Deklaration)
void setupUploadMain();
void setupUploadUi();

void A_Task_Web(void *pvParameter);
void webSocketCreate(void *pvParameter);
void registerWebRoutes(AsyncWebServer& server);


