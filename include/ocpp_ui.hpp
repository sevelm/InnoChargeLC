#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Web adapter only. No MicroOCPP calls may run in the Web task.
String ocpp_ui_state(bool includeLog);
bool ocpp_ui_save(JsonObjectConst settings, bool enabledOnly, String& error);
void ocpp_ui_clear_log();
bool ocpp_ui_save_local_rfid(JsonObjectConst settings, String& error);
void ocpp_ui_add_local_rfid(JsonObject state);
