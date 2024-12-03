#include "ESPAsyncWebServer.h"
#include "SPIFFS.h"

void A_Task_Web(void *pvParameter);
void webSocketCreate(void *pvParameter);
void registerWebRoutes(AsyncWebServer& server);
