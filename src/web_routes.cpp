#include "A_Task_Web.hpp"
#include "SPIFFS.h"
#include "ESPAsyncWebServer.h"


void registerWebRoutes(AsyncWebServer& server) {
    server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

    // Route definitions for all pages
    const char* routes[] = {
        "/network.html",
        "/interfaces.html",
        "/rfid.html",
        "/system.html",
        "/reserve1.html",
        "/reserve2.html",
        "/reserve3.html",
        "/reserve4.html",
        "/settings"
    };

    for (const char* route : routes) {
        server.on(route, HTTP_GET, [route](AsyncWebServerRequest *request) {
            request->send(SPIFFS, route, "text/html");
        });
    }

    // Pictures
    const char* images[] = {
        "/favicon.ico",
        "/innocharge.png",
        "/interface.png",
        "/rfid.png",
        "/computer.png",
        "/dashboard.png",
        "/settings.png"
    };

    const char* imageTypes[] = {
        "image/x-icon",
        "image/png",
        "image/png",
        "image/png",
        "image/png",
        "image/png"
    };

    for (size_t i = 0; i < sizeof(images) / sizeof(images[0]); ++i) {
        server.on(images[i], HTTP_GET, [images, imageTypes, i](AsyncWebServerRequest *request) {
            request->send(SPIFFS, images[i], imageTypes[i]);
        });
    }
}