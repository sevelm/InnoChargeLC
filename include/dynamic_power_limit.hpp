#ifndef DYNAMIC_POWER_LIMIT_HPP
#define DYNAMIC_POWER_LIMIT_HPP

#include <Arduino.h>
#include <ArduinoJson.h>

static constexpr uint8_t DYNAMIC_POWER_LIMIT_ROWS = 3;

void dynamic_power_limit_begin();
void dynamic_power_limit_poll();
void dynamic_power_limit_save_row(uint8_t index, bool enabled, const String& config);
void dynamic_power_limit_append_json(JsonObject root);

#endif
