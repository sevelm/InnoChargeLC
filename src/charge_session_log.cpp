#include "charge_session_log.hpp"

#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <time.h>
#include <algorithm>
#include <vector>
#include "freertos/semphr.h"
#include "control_pilot.hpp"

extern Preferences preferences;

static constexpr const char* SESSION_LOG_PATH = "/charge_sessions.json";
static constexpr const char* NEXT_TRANSACTION_KEY = "sessionTxNext";
static constexpr size_t MAX_STORED_SESSIONS = 500;
static constexpr size_t MAX_IMPORT_SESSIONS = 100;

struct charge_session_record_t {
    uint32_t transactionId = 0;
    String idTag;
    String userName;
    time_t startTime = 0;
    time_t stopTime = 0;
    uint32_t startMillis = 0;
    uint32_t stopMillis = 0;
    uint32_t durationSeconds = 0;
    float meterStartWh = 0.0f;
    float meterStopWh = 0.0f;
    float energyWh = 0.0f;
    String stopReason;
};

static std::vector<charge_session_record_t> s_sessions;
static charge_session_record_t s_activeSession;
static bool s_sessionActive = false;
static String s_pendingStopReason;
static SemaphoreHandle_t s_mutex = nullptr;

static void lock_log()
{
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
}

static void unlock_log()
{
    if (s_mutex) xSemaphoreGive(s_mutex);
}

static String isoTime(time_t value)
{
    if (value <= 0) return "";

    struct tm timeinfo;
    gmtime_r(&value, &timeinfo);

    char buffer[25];
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    return String(buffer);
}

static float currentMeterWh()
{
    return sdm.enrImp * 1000.0f;
}

static uint32_t nextTransactionId()
{
    uint32_t id = preferences.getUInt(NEXT_TRANSACTION_KEY, 1);
    if (id == 0) id = 1;
    preferences.putUInt(NEXT_TRANSACTION_KEY, id + 1);
    return id;
}

static void addRecordToJson(JsonArray sessions, const charge_session_record_t& record, bool active)
{
    JsonObject obj = sessions.add<JsonObject>();
    obj["transactionId"] = record.transactionId;
    obj["idTag"] = record.idTag;
    obj["userName"] = record.userName;
    obj["startTimeEpoch"] = (int64_t)record.startTime;
    obj["stopTimeEpoch"] = (int64_t)record.stopTime;
    obj["startTime"] = isoTime(record.startTime);
    obj["stopTime"] = isoTime(record.stopTime);
    obj["durationSeconds"] = record.durationSeconds;
    obj["meterStartWh"] = round(record.meterStartWh);
    obj["meterStopWh"] = round(record.meterStopWh);
    obj["energyWh"] = round(record.energyWh);
    obj["stopReason"] = record.stopReason;
    obj["active"] = active;
}

static bool save_locked()
{
    while (s_sessions.size() > MAX_STORED_SESSIONS) {
        s_sessions.erase(s_sessions.begin());
    }

    JsonDocument doc;
    JsonArray sessions = doc["sessions"].to<JsonArray>();
    for (const auto& record : s_sessions) {
        addRecordToJson(sessions, record, false);
    }

    File file = SPIFFS.open(SESSION_LOG_PATH, FILE_WRITE);
    if (!file) return false;
    serializeJson(doc, file);
    file.close();
    return true;
}

static void load_locked()
{
    s_sessions.clear();

    File file = SPIFFS.open(SESSION_LOG_PATH, FILE_READ);
    if (!file) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) return;

    JsonArray sessions = doc["sessions"].as<JsonArray>();
    for (JsonObject obj : sessions) {
        charge_session_record_t record;
        record.transactionId = obj["transactionId"] | 0;
        record.idTag = obj["idTag"] | "";
        record.userName = obj["userName"] | "";
        record.startTime = (time_t)(obj["startTimeEpoch"] | 0);
        record.stopTime = (time_t)(obj["stopTimeEpoch"] | 0);
        record.durationSeconds = obj["durationSeconds"] | 0;
        record.meterStartWh = obj["meterStartWh"] | 0.0f;
        record.meterStopWh = obj["meterStopWh"] | 0.0f;
        record.energyWh = obj["energyWh"] | 0.0f;
        record.stopReason = obj["stopReason"] | "";
        s_sessions.push_back(record);
    }

    while (s_sessions.size() > MAX_STORED_SESSIONS) {
        s_sessions.erase(s_sessions.begin());
    }
}

static bool recordFromJson(JsonObject obj, charge_session_record_t& record)
{
    record.transactionId = obj["transactionId"] | 0;
    record.idTag = obj["idTag"] | "";
    record.userName = obj["userName"] | "";
    record.startTime = (time_t)(obj["startTimeEpoch"] | 0);
    record.stopTime = (time_t)(obj["stopTimeEpoch"] | 0);
    record.durationSeconds = obj["durationSeconds"] | 0;
    record.meterStartWh = obj["meterStartWh"] | 0.0f;
    record.meterStopWh = obj["meterStopWh"] | 0.0f;
    record.energyWh = obj["energyWh"] | 0.0f;
    record.stopReason = obj["stopReason"] | "";
    return record.transactionId > 0;
}

static bool charge_session_log_import_document(JsonDocument& doc);

static void startSession()
{
    s_activeSession = charge_session_record_t{};
    s_activeSession.transactionId = nextTransactionId();
    s_activeSession.idTag = chargeAuthSession.idTag;
    s_activeSession.userName = chargeAuthSession.userName;
    s_activeSession.startTime = time(nullptr);
    s_activeSession.startMillis = millis();
    s_activeSession.meterStartWh = currentMeterWh();
    s_sessionActive = true;
}

static String stopReasonForState(charging_state_t state)
{
    if (s_pendingStopReason.length() > 0) {
        String reason = s_pendingStopReason;
        s_pendingStopReason = "";
        return reason;
    }
    if (!charging_authorization_allows_charging()) return "DeAuthorized";
    if (state == StateA_NotConnected) return "EVDisconnected";
    if (state == StateE_Error || state == StateF_Fault) return "Faulted";
    return "Stopped";
}

static void stopSession(charging_state_t state)
{
    s_activeSession.stopTime = time(nullptr);
    s_activeSession.stopMillis = millis();
    s_activeSession.meterStopWh = currentMeterWh();
    s_activeSession.energyWh = s_activeSession.meterStopWh - s_activeSession.meterStartWh;
    if (s_activeSession.energyWh < 0.0f) s_activeSession.energyWh = 0.0f;
    s_activeSession.durationSeconds = (s_activeSession.stopMillis - s_activeSession.startMillis) / 1000UL;
    s_activeSession.stopReason = stopReasonForState(state);

    s_sessions.push_back(s_activeSession);
    s_sessionActive = false;
    save_locked();
}

void charge_session_log_begin()
{
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    lock_log();
    load_locked();
    unlock_log();
}

void charge_session_log_update(charging_state_t state)
{
    const bool effectiveCharging =
        charging_authorization_allows_charging() &&
        (state == StateC_Charge || state == StateD_VentCharge);

    lock_log();
    if (effectiveCharging && !s_sessionActive) {
        startSession();
    } else if (!effectiveCharging && s_sessionActive) {
        stopSession(state);
    }
    unlock_log();
}

void charge_session_log_set_stop_reason(const char* reason)
{
    lock_log();
    s_pendingStopReason = reason ? reason : "";
    unlock_log();
}

String charge_session_log_to_json()
{
    JsonDocument doc;
    JsonArray sessions = doc["sessions"].to<JsonArray>();

    lock_log();
    for (auto it = s_sessions.rbegin(); it != s_sessions.rend(); ++it) {
        addRecordToJson(sessions, *it, false);
    }
    if (s_sessionActive) {
        charge_session_record_t active = s_activeSession;
        active.stopTime = time(nullptr);
        active.stopMillis = millis();
        active.meterStopWh = currentMeterWh();
        active.energyWh = active.meterStopWh - active.meterStartWh;
        if (active.energyWh < 0.0f) active.energyWh = 0.0f;
        active.durationSeconds = (active.stopMillis - active.startMillis) / 1000UL;
        addRecordToJson(sessions, active, true);
    }
    unlock_log();

    String out;
    serializeJson(doc, out);
    return out;
}

String charge_session_log_to_json_page(size_t offset, size_t limit)
{
    if (limit == 0 || limit > 100) {
        limit = 100;
    }

    JsonDocument doc;
    JsonArray sessions = doc["sessions"].to<JsonArray>();

    lock_log();
    const size_t total = s_sessions.size() + (s_sessionActive ? 1 : 0);
    if (offset >= total && total > 0) {
        offset = ((total - 1) / limit) * limit;
    }

    doc["sessionOffset"] = offset;
    doc["sessionLimit"] = limit;
    doc["sessionTotal"] = total;

    size_t index = 0;
    size_t added = 0;

    if (s_sessionActive) {
        if (index >= offset && added < limit) {
            charge_session_record_t active = s_activeSession;
            active.stopTime = time(nullptr);
            active.stopMillis = millis();
            active.meterStopWh = currentMeterWh();
            active.energyWh = active.meterStopWh - active.meterStartWh;
            if (active.energyWh < 0.0f) active.energyWh = 0.0f;
            active.durationSeconds = (active.stopMillis - active.startMillis) / 1000UL;
            addRecordToJson(sessions, active, true);
            added++;
        }
        index++;
    }

    for (auto it = s_sessions.rbegin(); it != s_sessions.rend() && added < limit; ++it) {
        if (index >= offset) {
            addRecordToJson(sessions, *it, false);
            added++;
        }
        index++;
    }
    unlock_log();

    String out;
    serializeJson(doc, out);
    return out;
}

bool charge_session_log_import_json(const String& json)
{
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) return false;
    return charge_session_log_import_document(doc);
}

static bool charge_session_log_import_document(JsonDocument& doc)
{
    JsonArray sessions = doc["sessions"].as<JsonArray>();
    if (sessions.isNull()) return false;
    if (sessions.size() > MAX_IMPORT_SESSIONS) return false;

    std::vector<charge_session_record_t> imported;
    uint32_t maxTransactionId = 0;

    for (JsonObject obj : sessions) {
        if (obj["active"] | false) continue;

        charge_session_record_t record;
        if (!recordFromJson(obj, record)) continue;
        if (record.transactionId > maxTransactionId) {
            maxTransactionId = record.transactionId;
        }
        imported.push_back(record);
    }

    std::sort(imported.begin(), imported.end(),
              [](const charge_session_record_t& a, const charge_session_record_t& b) {
                  return a.transactionId < b.transactionId;
              });

    while (imported.size() > MAX_STORED_SESSIONS) {
        imported.erase(imported.begin());
    }

    lock_log();
    for (const auto& importedRecord : imported) {
        auto existing = std::find_if(s_sessions.begin(), s_sessions.end(),
                                     [&](const charge_session_record_t& record) {
                                         return record.transactionId == importedRecord.transactionId;
                                     });
        if (existing != s_sessions.end()) {
            *existing = importedRecord;
        } else {
            s_sessions.push_back(importedRecord);
        }
    }

    std::sort(s_sessions.begin(), s_sessions.end(),
              [](const charge_session_record_t& a, const charge_session_record_t& b) {
                  return a.transactionId < b.transactionId;
              });

    while (s_sessions.size() > MAX_STORED_SESSIONS) {
        s_sessions.erase(s_sessions.begin());
    }

    bool ok = save_locked();
    unlock_log();

    uint32_t nextId = preferences.getUInt(NEXT_TRANSACTION_KEY, 1);
    if (maxTransactionId >= nextId) {
        preferences.putUInt(NEXT_TRANSACTION_KEY, maxTransactionId + 1);
    }

    return ok;
}

bool charge_session_log_import_file(const char* path)
{
    File file = SPIFFS.open(path, FILE_READ);
    if (!file) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) return false;

    return charge_session_log_import_document(doc);
}
