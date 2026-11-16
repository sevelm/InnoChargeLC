#pragma once

#include <ArduinoJson.h>
#include <stdint.h>
#include <string.h>

// Kleine, hardwarefreie Pruefungen; werden auch im PC-Regressionstest verwendet.
namespace ocpp_policy {
enum class Interval { Unsupported, Heartbeat, MeterValues };
enum class RetryKey { Unsupported, Attempts, Interval };
constexpr const char* MAX_CHARGING_POWER_KEY = "InnoChargeMaxChargingPowerKW";

// kW-Text exakt ins bestehende 0,1-kW-Raster umrechnen; keine Aufrundung.
inline bool parse_max_charging_power(const char* text, size_t length, uint16_t& tenths) {
    if (!text || !length || length > 12) return false;
    size_t pos = 0;
    unsigned whole = 0;
    while (pos < length && text[pos] >= '0' && text[pos] <= '9') {
        whole = whole * 10 + unsigned(text[pos++] - '0');
        if (whole > 22) return false;
    }
    if (!pos) return false;
    unsigned fraction = 0;
    if (pos < length) {
        if (text[pos++] != '.' || pos == length) return false;
        if (text[pos] < '0' || text[pos] > '9') return false;
        fraction = unsigned(text[pos++] - '0');
        while (pos < length) {
            if (text[pos++] != '0') return false;
        }
    }
    const unsigned value = whole * 10 + fraction;
    if (value < 42 || value > 220) return false;
    tenths = uint16_t(value);
    return true; // DIP-Grenze und Speichern prueft danach der bestehende Setter.
}

inline Interval interval_key(const char* key) {
    if (!strcmp(key, "HeartbeatInterval")) return Interval::Heartbeat;
    if (!strcmp(key, "MeterValueSampleInterval")) return Interval::MeterValues;
    return Interval::Unsupported;
}

inline bool parse_interval(Interval key, const char* text, size_t length, uint32_t& seconds) {
    if (key == Interval::Unsupported || !text || !length || length > 5) return false;
    uint32_t parsed = 0;
    for (size_t i = 0; i < length; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        parsed = parsed * 10 + uint32_t(text[i] - '0');
    }
    // Heartbeat=0 would flood MicroOCPP's queue. MeterValues=0 disables periodic samples.
    if (parsed > 86400 || (key == Interval::Heartbeat && parsed == 0)) return false;
    seconds = parsed;
    return true;
}

inline RetryKey retry_key(const char* key) {
    if (!strcmp(key, "TransactionMessageAttempts")) return RetryKey::Attempts;
    if (!strcmp(key, "TransactionMessageRetryInterval")) return RetryKey::Interval;
    return RetryKey::Unsupported;
}

inline bool parse_retry(RetryKey key, const char* text, size_t length, uint32_t& value) {
    if (key == RetryKey::Unsupported || !text || !length || length > 5) return false;
    uint32_t parsed = 0;
    for (size_t i = 0; i < length; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        parsed = parsed * 10 + uint32_t(text[i] - '0');
    }
    if (key == RetryKey::Attempts ? (parsed < 1 || parsed > 100) : parsed > 86400) return false;
    value = parsed;
    return true;
}

inline bool string_equals(JsonVariantConst value, const char* expected) {
    if (!value.is<const char*>()) return false;
    const JsonString text = value.as<JsonString>();
    return text.size() == strlen(expected) && !strcmp(text.c_str(), expected);
}

inline bool start_matches(JsonObjectConst payload, const char* tag, int32_t meterWh,
                          const char* timestamp) {
    // Reservation is not implemented. Do not invent/drop any optional start fields.
    return payload.size() == 4 && payload["connectorId"].is<int>() &&
        payload["connectorId"].as<int>() == 1 && payload["meterStart"].is<int32_t>() &&
        payload["meterStart"].as<int32_t>() == meterWh && meterWh >= 0 &&
        string_equals(payload["idTag"], tag) && string_equals(payload["timestamp"], timestamp);
}

inline bool start_reply_id(JsonObjectConst payload, int32_t& transactionId) {
    if (!payload["transactionId"].is<int32_t>() || payload["transactionId"].as<int32_t>() < 0)
        return false;
    const auto status = payload["idTagInfo"]["status"];
    const bool valid = string_equals(status, "Accepted") || string_equals(status, "Blocked") ||
        string_equals(status, "Expired") || string_equals(status, "Invalid") ||
        string_equals(status, "ConcurrentTx");
    if (!valid) return false;
    // Even a deauthorized start has a backend ID to close; this never grants charging.
    transactionId = payload["transactionId"].as<int32_t>();
    return true;
}

inline bool replay_start_allowed(bool uncertain, bool idKnown, bool stopConfirmed,
                                 bool originalSaved, bool validBoundaries) {
    return uncertain && !idKnown && !stopConfirmed && originalSaved && validBoundaries;
}
} // namespace ocpp_policy
