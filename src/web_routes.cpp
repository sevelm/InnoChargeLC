#include "A_Task_Web.hpp"
#include "SPIFFS.h"
#include "ESPAsyncWebServer.h"


void registerWebRoutes(AsyncWebServer& server) {
    (void)server;
    // Statische Dateien und Login-Routen werden zentral in A_Task_Web.cpp registriert.
}
