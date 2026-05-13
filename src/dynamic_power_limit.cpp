#include "dynamic_power_limit.hpp"
#include "control_pilot.hpp"

#include <Preferences.h>
#include <WiFiClient.h>

extern Preferences preferences;

struct dynamic_power_limit_row_t {
    bool enabled = false;
    String config;
    String status = "Not read yet";
};

static dynamic_power_limit_row_t rows[DYNAMIC_POWER_LIMIT_ROWS];
static uint16_t transactionId = 1;
static TaskHandle_t pollTaskHandle = nullptr;

static void dynamicPowerLimitTask(void*)
{
    vTaskDelay(pdMS_TO_TICKS(10000));
    for (;;) {
        dynamic_power_limit_poll();
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static String keyFor(uint8_t index, const char* suffix)
{
    return String("dpl") + index + suffix;
}

static bool getConfigValue(const String& config, const char* key, String& value)
{
    String prefix = String(key) + "=";
    int start = config.indexOf(prefix);
    if (start < 0) return false;
    start += prefix.length();
    int end = config.indexOf(';', start);
    value = end >= 0 ? config.substring(start, end) : config.substring(start);
    value.trim();
    return value.length() > 0;
}

static bool readConfigInt(const String& config, const char* key, int& value)
{
    String text;
    if (!getConfigValue(config, key, text)) return false;
    value = text.toInt();
    return true;
}

static float readConfigFloat(const String& config, const char* key, float fallback)
{
    String text;
    if (!getConfigValue(config, key, text)) return fallback;
    return text.toFloat();
}

static bool readModbusValue(const String& config, float& value)
{
    String ipText;
    String type;
    int port = 502;
    int fc = 3;
    int reg = -1;
    int unit = 1;

    if (!getConfigValue(config, "ip", ipText)) return false;
    readConfigInt(config, "port", port);
    readConfigInt(config, "fc", fc);
    if (!readConfigInt(config, "reg", reg)) return false;
    readConfigInt(config, "unit", unit);
    if (!getConfigValue(config, "type", type)) type = "float";
    type.toLowerCase();

    uint16_t regCount = (type == "float" || type == "u32" || type == "s32") ? 2 : 1;
    IPAddress ip;
    if (!ip.fromString(ipText)) return false;
    if (fc != 3 && fc != 4) return false;

    WiFiClient client;
    client.setTimeout(300);
    if (!client.connect(ip, (uint16_t)port, 300)) {
        return false;
    }

    uint8_t request[12];
    uint16_t tx = transactionId++;
    request[0] = tx >> 8;
    request[1] = tx & 0xFF;
    request[2] = 0;
    request[3] = 0;
    request[4] = 0;
    request[5] = 6;
    request[6] = (uint8_t)unit;
    request[7] = (uint8_t)fc;
    request[8] = (uint16_t)reg >> 8;
    request[9] = (uint16_t)reg & 0xFF;
    request[10] = regCount >> 8;
    request[11] = regCount & 0xFF;

    if (client.write(request, sizeof(request)) != sizeof(request)) {
        client.stop();
        return false;
    }

    uint8_t response[13];
    size_t expected = 9 + regCount * 2;
    size_t got = 0;
    uint32_t startMs = millis();
    while (got < expected && millis() - startMs < 300) {
        while (client.available() && got < expected) {
            response[got++] = client.read();
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    client.stop();

    if (got < expected) return false;
    if (response[0] != request[0] || response[1] != request[1]) return false;
    if (response[7] & 0x80) return false;
    if (response[7] != fc) return false;
    if (response[8] != regCount * 2) return false;

    uint16_t r0 = ((uint16_t)response[9] << 8) | response[10];
    uint16_t r1 = regCount > 1 ? (((uint16_t)response[11] << 8) | response[12]) : 0;

    if (type == "float") {
        uint32_t raw = ((uint32_t)r0 << 16) | r1;
        memcpy(&value, &raw, sizeof(value));
    } else if (type == "s16") {
        value = (int16_t)r0;
    } else if (type == "u32") {
        value = ((uint32_t)r0 << 16) | r1;
    } else if (type == "s32") {
        value = (int32_t)(((uint32_t)r0 << 16) | r1);
    } else {
        value = r0;
    }

    value *= readConfigFloat(config, "scale", 1.0f);
    return true;
}

void dynamic_power_limit_begin()
{
    for (uint8_t i = 0; i < DYNAMIC_POWER_LIMIT_ROWS; i++) {
        rows[i].enabled = preferences.getBool(keyFor(i, "en").c_str(), false);
        rows[i].config = preferences.getString(keyFor(i, "cfg").c_str(), "");
        rows[i].status = rows[i].enabled ? "Waiting for read" : "Disabled";
    }

    if (!pollTaskHandle) {
        xTaskCreatePinnedToCore(
            dynamicPowerLimitTask,
            "DynPowerLimit",
            4096,
            nullptr,
            1,
            &pollTaskHandle,
            1
        );
    }
}

void dynamic_power_limit_save_row(uint8_t index, bool enabled, const String& config)
{
    if (index >= DYNAMIC_POWER_LIMIT_ROWS) return;

    String cleanConfig = config;
    cleanConfig.trim();
    if (cleanConfig.length() > 160) {
        cleanConfig = cleanConfig.substring(0, 160);
    }

    rows[index].enabled = enabled;
    rows[index].config = cleanConfig;
    rows[index].status = enabled ? "Waiting for read" : "Disabled";

    preferences.putBool(keyFor(index, "en").c_str(), enabled);
    preferences.putString(keyFor(index, "cfg").c_str(), cleanConfig);
}

void dynamic_power_limit_poll()
{
    for (uint8_t i = 0; i < DYNAMIC_POWER_LIMIT_ROWS; i++) {
        if (!rows[i].enabled) {
            rows[i].status = "Disabled";
            continue;
        }
        if (rows[i].config.length() == 0) {
            rows[i].status = "No config";
            continue;
        }

        rows[i].status = "Reading...";
        float value = 0.0f;
        if (readModbusValue(rows[i].config, value)) {
            String mode;
            getConfigValue(rows[i].config, "mode", mode);
            mode.toLowerCase();

            if (mode == "max_kw") {
                float limit = readConfigFloat(rows[i].config, "limit", 0.0f);
                float minPower = readConfigFloat(rows[i].config, "min", 0.0f);
                float setPower = limit - value;

                if (limit > 0.0f) {
                    if (setPower < minPower) {
                        set_charging_power(0);
                        rows[i].status = "Read: " + String(value, 2) + " kW -> Set: 0.00 kW";
                    } else {
                        set_charging_power(setPower * 10.0f);
                        rows[i].status = "Read: " + String(value, 2) + " kW -> Set: " + String(setPower, 2) + " kW";
                    }
                } else {
                    rows[i].status = "Read: " + String(value, 2) + " kW -> Missing limit";
                }
            } else {
                rows[i].status = "Read: " + String(value, 2);
            }
        } else {
            rows[i].status = "Read error";
        }
    }
}

void dynamic_power_limit_append_json(JsonObject root)
{
    JsonArray arr = root["dynamicPowerLimit"].to<JsonArray>();

    for (uint8_t i = 0; i < DYNAMIC_POWER_LIMIT_ROWS; i++) {
        JsonObject row = arr.add<JsonObject>();
        row["index"] = i;
        row["enabled"] = rows[i].enabled;
        row["config"] = rows[i].config;
        row["status"] = rows[i].status;
    }
}
