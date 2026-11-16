#include "charge_session_log.hpp"

#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <time.h>
#include <algorithm>
#include <vector>
#include "freertos/semphr.h"
#include "control_pilot.hpp"
#include "A_Task_OCPP.hpp"
#include "ocpp_policy.hpp"
#include <cmath>
#include <memory>

extern Preferences preferences;

static constexpr const char* SESSION_LOG_PATH = "/charge_sessions.json";
static constexpr const char* SESSION_ALT_PATH = "/charge_sessions.alt.json";
static constexpr const char* NEXT_TRANSACTION_KEY = "sessionTxNext";
static constexpr size_t MAX_STORED_SESSIONS = 500;
static constexpr size_t MAX_IMPORT_SESSIONS = 100;

struct session_backend_t { String url; String chargePointId; };
// Share the repeated backend identity instead of allocating 320 bytes per row.
struct stored_ocpp_link_t {
    int32_t transactionId = -1;
    session_ocpp_state_t start = session_ocpp_state_t::None;
    session_ocpp_state_t stop = session_ocpp_state_t::None;
    bool local = false;
    String idTag;
    std::shared_ptr<const session_backend_t> backend;
};
struct charge_session_record_t {
    uint32_t transactionId = 0;
    uint32_t authorizationStamp = 0; // RAM correlation only; never reused after reboot
    stored_ocpp_link_t ocpp;
    bool recoverStop = false; // Persistenter Nachsendeauftrag; niemals eine Ladefreigabe.
    bool recoverStart = false;
    String startRequest; // Exakter Wire-Payload, nur bis zum Erhalt der Backend-ID behalten.
    bool meterStartValid = false;
    bool meterStopValid = false;
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
static bool s_storageFull = false;
static String s_pendingStopReason;
static SemaphoreHandle_t s_mutex = nullptr;
static SemaphoreHandle_t s_fileMutex = nullptr;
static uint32_t s_revision = 0, s_savedRevision = 0;
static uint32_t s_nextId = 1, s_reservedUntil = 1;
static bool s_mainNewest = false;
static uint32_t s_checkpointMs = 0;
static uint32_t s_preparedStamp = 0;
static stored_ocpp_link_t s_preparedLink;
static std::weak_ptr<const session_backend_t> s_lastBackend;
static bool recordFromJson(JsonObject obj, charge_session_record_t& record);

static std::shared_ptr<const session_backend_t> backendIdentity(const char* url, const char* id)
{
    auto previous = s_lastBackend.lock();
    if (previous && previous->url == url && previous->chargePointId == id) return previous;
    auto value = std::make_shared<session_backend_t>();
    value->url = url;
    value->chargePointId = id;
    s_lastBackend = value;
    return value;
}

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
    // IDs are reserved in NVS outside the CP task. Gaps after reboot are intentional.
    return s_nextId < s_reservedUntil ? s_nextId++ : 0;
}

static const char* deliveryState(session_ocpp_state_t state) {
    static const char* names[] = {"none", "pending", "sent", "confirmed", "uncertain", "failed"};
    return names[static_cast<unsigned>(state)];
}
static session_ocpp_state_t readDeliveryState(const char* text) {
    for (unsigned i = 0; i <= unsigned(session_ocpp_state_t::Failed); ++i)
        if (!strcmp(text, deliveryState(static_cast<session_ocpp_state_t>(i))))
            return static_cast<session_ocpp_state_t>(i);
    return session_ocpp_state_t::None;
}

static void addRecordToJson(JsonArray sessions, const charge_session_record_t& record, bool active, bool appView = false)
{
    JsonObject obj = sessions.add<JsonObject>();
    obj["transactionId"] = record.transactionId;
    if (record.ocpp.transactionId >= 0) obj["ocppTransactionId"] = record.ocpp.transactionId;
    else obj["ocppTransactionId"] = nullptr;
    obj["ocppIdTag"] = record.ocpp.idTag;
    obj["ocppStart"] = deliveryState(record.ocpp.start);
    obj["ocppStop"] = deliveryState(record.ocpp.stop);
    obj["ocppLocal"] = record.ocpp.local;
    if (!appView) {
        if (record.recoverStop) obj["ocppRecoverStop"] = true;
        if (record.recoverStart) obj["ocppRecoverStart"] = true;
        if (!record.startRequest.isEmpty()) obj["ocppStartRequest"] = record.startRequest;
        if (record.ocpp.backend) {
            obj["ocppBackendUrl"] = record.ocpp.backend->url;
            obj["ocppChargePointId"] = record.ocpp.backend->chargePointId;
        }
    }
    obj["meterStartValid"] = record.meterStartValid;
    obj["meterStopValid"] = record.meterStopValid;
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
        auto removable = std::find_if(s_sessions.begin(), s_sessions.end(), [](const charge_session_record_t& r) {
            return r.ocpp.start == session_ocpp_state_t::None || r.ocpp.stop == session_ocpp_state_t::Confirmed;
        });
        if (removable == s_sessions.end()) break; // never silently discard undelivered transactions
        s_sessions.erase(removable);
    }
    ++s_revision; // Flash I/O is performed by Low/OCPP, never the CP task.
    return true;
}

static bool hasRecoveryBoundaries(const charge_session_record_t& record)
{
    return record.ocpp.backend &&
        record.ocpp.backend->url.length() && record.ocpp.backend->chargePointId.length() &&
        record.meterStartValid && record.meterStopValid &&
        std::isfinite(record.meterStartWh) && std::isfinite(record.meterStopWh) &&
        record.meterStartWh >= 0 && record.meterStopWh >= record.meterStartWh &&
        double(record.meterStopWh) <= INT32_MAX &&
        record.startTime >= 1577836800 && record.stopTime >= record.startTime;
}

static bool canRecoverStop(const charge_session_record_t& record)
{
    return record.ocpp.start == session_ocpp_state_t::Confirmed &&
        record.ocpp.transactionId >= 0 && hasRecoveryBoundaries(record);
}

static bool recoverAfterBoot(charge_session_record_t& record, bool wasActive)
{
    const auto previousStart = record.ocpp.start;
    const auto previousStop = record.ocpp.stop;
    const bool previousRecovery = record.recoverStop;
    const bool previousStartRecovery = record.recoverStart;
    if (wasActive) {
        record.stopReason = "PowerLoss";
        // Nur den vorhandenen Checkpoint verwenden; keine neue Endmessung erfinden.
        record.ocpp.stop = record.ocpp.start == session_ocpp_state_t::None ?
            session_ocpp_state_t::None : session_ocpp_state_t::Uncertain;
    }
    if (record.ocpp.start == session_ocpp_state_t::Sent)
        record.ocpp.start = session_ocpp_state_t::Uncertain;

    // Altbestand ohne gesicherten Wire-Payload bleibt Review. Niemals Startdaten erfinden.
    record.recoverStart = ocpp_policy::replay_start_allowed(
        record.ocpp.start == session_ocpp_state_t::Uncertain, record.ocpp.transactionId >= 0,
        previousStop == session_ocpp_state_t::Confirmed, !record.startRequest.isEmpty(),
        hasRecoveryBoundaries(record));
    if (record.recoverStart) record.ocpp.stop = session_ocpp_state_t::Pending;

    const bool interrupted = wasActive || previousRecovery ||
        previousStop == session_ocpp_state_t::Pending || previousStop == session_ocpp_state_t::Sent;
    record.recoverStop = interrupted && previousStop != session_ocpp_state_t::Confirmed &&
        canRecoverStop(record);
    if (record.recoverStop) {
        // Auch nach erneutem Neustart wieder versuchen, bis die Bestaetigung gespeichert ist.
        record.ocpp.stop = session_ocpp_state_t::Pending;
    } else if (!record.recoverStart && (record.ocpp.stop == session_ocpp_state_t::Sent ||
        (record.ocpp.start == session_ocpp_state_t::Confirmed && record.ocpp.stop == session_ocpp_state_t::Pending))) {
        record.ocpp.stop = session_ocpp_state_t::Uncertain;
    }
    return wasActive || previousStart != record.ocpp.start || previousStop != record.ocpp.stop ||
        previousRecovery != record.recoverStop || previousStartRecovery != record.recoverStart;
}

static void load_locked()
{
    s_sessions.clear();

    JsonDocument doc;
    uint32_t bestRevision = 0;
    const char* best = nullptr;
    for (const char* path : {SESSION_LOG_PATH, SESSION_ALT_PATH}) {
        File file = SPIFFS.open(path, FILE_READ);
        if (!file) continue;
        const auto err = deserializeJson(doc, file);
        file.close();
        if (err || !doc["sessions"].is<JsonArray>()) continue;
        const bool legacy = doc["format"].isNull() && path == SESSION_LOG_PATH;
        if (!legacy && (!(doc["complete"] | false) ||
            doc["count"].as<size_t>() != doc["sessions"].size())) continue;
        const uint32_t revision = doc["revision"] | 0U;
        if (!best || revision >= bestRevision) { best = path; bestRevision = revision; }
    }
    if (!best) return;
    File file = SPIFFS.open(best, FILE_READ);
    if (!file || deserializeJson(doc, file)) return;
    file.close();
    s_mainNewest = best == SESSION_LOG_PATH;
    s_revision = s_savedRevision = bestRevision;

    JsonArray sessions = doc["sessions"].as<JsonArray>();
    for (JsonObject obj : sessions) {
        charge_session_record_t record;
        if (!recordFromJson(obj, record)) continue;
        if (recoverAfterBoot(record, obj["active"] | false)) ++s_revision;
        s_sessions.push_back(record);
    }
}

static bool recordFromJson(JsonObject obj, charge_session_record_t& record)
{
    record.transactionId = obj["transactionId"] | 0;
    record.ocpp.transactionId = obj["ocppTransactionId"] | -1;
    record.ocpp.start = readDeliveryState(obj["ocppStart"] | "none");
    record.ocpp.stop = readDeliveryState(obj["ocppStop"] | "none");
    record.recoverStop = obj["ocppRecoverStop"] | false;
    record.recoverStart = obj["ocppRecoverStart"] | false;
    record.startRequest = obj["ocppStartRequest"] | "";
    if (record.startRequest.length() > 512) record.startRequest = "";
    record.ocpp.local = obj["ocppLocal"] | false;
    record.ocpp.idTag = obj["ocppIdTag"] | "";
    if (record.ocpp.start != session_ocpp_state_t::None)
        record.ocpp.backend = backendIdentity(obj["ocppBackendUrl"] | "", obj["ocppChargePointId"] | "");
    record.meterStartValid = obj["meterStartValid"] | false;
    record.meterStopValid = obj["meterStopValid"] | false;
    record.idTag = obj["idTag"] | "";
    record.userName = obj["userName"] | "";
    record.startTime = (time_t)(obj["startTimeEpoch"] | 0);
    record.stopTime = (time_t)(obj["stopTimeEpoch"] | 0);
    record.durationSeconds = obj["durationSeconds"] | 0;
    record.meterStartWh = obj["meterStartWh"] | 0.0f;
    record.meterStopWh = obj["meterStopWh"] | 0.0f;
    record.energyWh = obj["energyWh"] | 0.0f;
    record.stopReason = obj["stopReason"] | "";
    return record.transactionId > 0 && record.transactionId < UINT32_MAX - 1024;
}

static bool charge_session_log_import_document(JsonDocument& doc);

static bool readSessionMeter(float& wh) {
    ocpp_meter_sample_t sample{};
    portENTER_CRITICAL(&ocppMeterMux);
    sample = ocppMeterSample;
    portEXIT_CRITICAL(&ocppMeterMux);
    wh = std::isfinite(sample.importKWh) ? sample.importKWh * 1000.0f : 0.0f;
    return sdm.enable && sample.valid && uint32_t(millis() - sample.capturedAtMs) <= 6000 &&
        std::isfinite(sample.importKWh) && sample.importKWh >= 0 &&
        double(sample.importKWh) * 1000.0 <= INT32_MAX;
}

static void startSession()
{
    if (s_sessions.size() >= MAX_STORED_SESSIONS) {
        auto old = std::find_if(s_sessions.begin(), s_sessions.end(), [](const charge_session_record_t& r) {
            return r.ocpp.start == session_ocpp_state_t::None || r.ocpp.stop == session_ocpp_state_t::Confirmed;
        });
        if (old == s_sessions.end()) { s_storageFull = true; return; }
        s_sessions.erase(old);
    }
    s_activeSession = charge_session_record_t{};
    s_activeSession.transactionId = nextTransactionId();
    if (!s_activeSession.transactionId) { s_storageFull = true; return; }
    s_storageFull = false;
    s_activeSession.authorizationStamp = chargeAuthSession.authorizationGrantedMillis;
    if (s_preparedStamp && s_preparedStamp == s_activeSession.authorizationStamp)
        s_activeSession.ocpp = s_preparedLink;
    s_activeSession.idTag = chargeAuthSession.idTag;
    s_activeSession.userName = chargeAuthSession.userName;
    s_activeSession.startTime = time(nullptr);
    s_activeSession.startMillis = millis();
    s_activeSession.meterStartValid = readSessionMeter(s_activeSession.meterStartWh);
    s_activeSession.meterStopWh = s_activeSession.meterStartWh;
    // Initialer Checkpoint fuer einen Ausfall vor dem ersten 30-s-Zwischenstand.
    // Das bleibt ein letzter BEKANNTER Stand, keine erfundene Endmessung.
    s_activeSession.meterStopValid = s_activeSession.meterStartValid;
    s_activeSession.stopTime = s_activeSession.startTime;
    s_sessionActive = true;
    save_locked();
}

static String stopReasonForState(charging_state_t state)
{
    if (s_pendingStopReason.length() > 0) {
        String reason = s_pendingStopReason;
        s_pendingStopReason = "";
        return reason;
    }
    if (state == StateA_NotConnected) return "EVDisconnected";
    if (state == StateE_Error || state == StateF_Fault) return "Faulted";
    if (!charging_authorization_allows_charging()) return "DeAuthorized";
    return "Stopped";
}

static void stopSession(charging_state_t state)
{
    s_activeSession.stopTime = time(nullptr);
    s_activeSession.stopMillis = millis();
    s_activeSession.meterStopValid = readSessionMeter(s_activeSession.meterStopWh);
    s_activeSession.energyWh = s_activeSession.meterStopWh - s_activeSession.meterStartWh;
    if (s_activeSession.energyWh < 0.0f) s_activeSession.energyWh = 0.0f;
    s_activeSession.durationSeconds = (s_activeSession.stopMillis - s_activeSession.startMillis) / 1000UL;
    s_activeSession.stopReason = stopReasonForState(state);
    if (s_activeSession.ocpp.start != session_ocpp_state_t::None)
        s_activeSession.ocpp.stop = session_ocpp_state_t::Pending;

    s_sessions.push_back(s_activeSession);
    s_sessionActive = false;
    save_locked();
}

void charge_session_log_begin()
{
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    if (!s_fileMutex) s_fileMutex = xSemaphoreCreateMutex();
    lock_log();
    load_locked();
    s_nextId = preferences.getUInt(NEXT_TRANSACTION_KEY, 1);
    for (const auto& record : s_sessions) s_nextId = std::max(s_nextId, record.transactionId + 1);
    if (!s_nextId) s_nextId = 1;
    s_reservedUntil = s_nextId;
    const uint32_t limit = s_nextId + 1024;
    if (limit > s_nextId && preferences.putUInt(NEXT_TRANSACTION_KEY, limit) == sizeof(uint32_t))
        s_reservedUntil = limit;
    unlock_log();
}

void charge_session_log_update(charging_state_t state)
{
    const bool authorized = charging_authorization_allows_charging();

    const bool effectiveCharging =
        authorized &&
        (state == StateC_Charge || state == StateD_VentCharge);

    // Keep the session open during State B pauses, including phase switching.
    const bool sessionEnded =
        state == StateA_NotConnected ||
        state == StateE_Error ||
        state == StateF_Fault ||
        !authorized;

    lock_log();
    if (s_sessionActive && effectiveCharging &&
        s_activeSession.authorizationStamp != chargeAuthSession.authorizationGrantedMillis) {
        stopSession(state);
    }
    if (effectiveCharging && !s_sessionActive) {
        startSession();
    } else if (sessionEnded && s_sessionActive) {
        stopSession(state);
    }
    unlock_log();
}

void charge_session_log_set_stop_reason(const char* reason)
{
    lock_log();
    s_pendingStopReason = s_sessionActive && reason ? reason : "";
    unlock_log();
}

bool charge_session_log_has_active_session()
{
    lock_log();
    const bool active = s_sessionActive;
    unlock_log();
    return active;
}

static stored_ocpp_link_t storeLink(const session_ocpp_link_t& link) {
    stored_ocpp_link_t stored;
    stored.transactionId = link.transactionId;
    stored.start = link.start;
    stored.stop = link.stop;
    stored.local = link.local;
    stored.idTag = link.idTag;
    if (link.start != session_ocpp_state_t::None)
        stored.backend = backendIdentity(link.backendUrl, link.chargePointId);
    return stored;
}

static void copyOcppRecord(const charge_session_record_t& source, bool active, session_ocpp_record_t& out) {
    out = {};
    out.localId = source.transactionId;
    out.authorizationStamp = source.authorizationStamp;
    out.active = active;
    out.startTime = source.startTime;
    out.stopTime = source.stopTime;
    out.meterStartValid = source.meterStartValid && std::isfinite(source.meterStartWh) &&
        source.meterStartWh >= 0 && double(source.meterStartWh) <= INT32_MAX;
    out.meterStopValid = source.meterStopValid && std::isfinite(source.meterStopWh) &&
        source.meterStopWh >= source.meterStartWh && double(source.meterStopWh) <= INT32_MAX;
    if (out.meterStartValid) out.meterStartWh = int32_t(std::llround(source.meterStartWh));
    if (out.meterStopValid) out.meterStopWh = int32_t(std::llround(source.meterStopWh));
    strlcpy(out.stopReason, source.stopReason.c_str(), sizeof(out.stopReason));
    out.ocpp.transactionId = source.ocpp.transactionId;
    out.ocpp.start = source.ocpp.start;
    out.ocpp.stop = source.ocpp.stop;
    out.ocpp.local = source.ocpp.local;
    strlcpy(out.ocpp.idTag, source.ocpp.idTag.c_str(), sizeof(out.ocpp.idTag));
    if (source.ocpp.backend) {
        strlcpy(out.ocpp.backendUrl, source.ocpp.backend->url.c_str(), sizeof(out.ocpp.backendUrl));
        strlcpy(out.ocpp.chargePointId, source.ocpp.backend->chargePointId.c_str(), sizeof(out.ocpp.chargePointId));
    }
}

void charge_session_log_prepare_ocpp(uint32_t stamp, const session_ocpp_link_t& link) {
    lock_log();
    s_preparedStamp = stamp;
    s_preparedLink = storeLink(link);
    unlock_log();
}

bool charge_session_log_get_ocpp(uint32_t id, uint32_t stamp, session_ocpp_record_t& out) {
    lock_log();
    auto matches = [=](const charge_session_record_t& r) {
        return id ? r.transactionId == id : stamp && r.authorizationStamp == stamp;
    };
    bool found = false;
    if (s_sessionActive && matches(s_activeSession)) {
        copyOcppRecord(s_activeSession, true, out);
        found = true;
    } else for (const auto& record : s_sessions) if (matches(record)) {
        copyOcppRecord(record, false, out);
        found = true;
        break;
    }
    unlock_log();
    return found;
}

bool charge_session_log_next_ocpp(const char* url, const char* point, session_ocpp_record_t& out) {
    lock_log();
    auto matches = [&](const charge_session_record_t& r) {
        return r.ocpp.start == session_ocpp_state_t::Pending &&
            r.ocpp.stop != session_ocpp_state_t::Uncertain && r.ocpp.backend &&
            r.ocpp.backend->url == url && r.ocpp.backend->chargePointId == point;
    };
    bool found = false;
    // Old records first; reporting never changes a newer local permission.
    for (const auto& r : s_sessions) if (matches(r)) {
        copyOcppRecord(r, false, out);
        found = true;
        break;
    }
    if (!found && s_sessionActive && matches(s_activeSession)) {
        copyOcppRecord(s_activeSession, true, out);
        found = true;
    }
    unlock_log();
    return found;
}

bool charge_session_log_next_recovered_stop(const char* url, const char* point, session_ocpp_record_t& out) {
    lock_log();
    bool found = false;
    for (const auto& record : s_sessions) {
        if (record.recoverStop && canRecoverStop(record) &&
            (record.ocpp.stop == session_ocpp_state_t::Pending || record.ocpp.stop == session_ocpp_state_t::Sent) &&
            record.ocpp.backend->url == url && record.ocpp.backend->chargePointId == point) {
            copyOcppRecord(record, false, out);
            found = true;
            break;
        }
    }
    unlock_log();
    return found;
}

bool charge_session_log_capture_start(uint32_t id, const String& payload) {
    if (payload.isEmpty() || payload.length() > 512) return false;
    lock_log();
    charge_session_record_t* target = nullptr;
    if (s_sessionActive && s_activeSession.transactionId == id) target = &s_activeSession;
    for (auto& record : s_sessions) if (record.transactionId == id) target = &record;
    bool ok = target && (target->ocpp.start == session_ocpp_state_t::Sent ||
        target->ocpp.start == session_ocpp_state_t::Pending);
    if (ok && !target->startRequest.isEmpty()) ok = target->startRequest == payload;
    if (ok && target->startRequest.isEmpty()) {
        target->startRequest = payload;
        target->ocpp.start = session_ocpp_state_t::Sent;
        save_locked();
    }
    unlock_log();
    return ok;
}

bool charge_session_log_next_recovered_start(const char* url, const char* point,
    session_ocpp_record_t& out, String& payload) {
    lock_log();
    bool found = false;
    for (const auto& record : s_sessions) {
        if (!record.recoverStart || record.ocpp.start != session_ocpp_state_t::Uncertain ||
            record.ocpp.transactionId >= 0 || record.startRequest.isEmpty() ||
            !hasRecoveryBoundaries(record)) continue;
        if (record.ocpp.backend->url != url || record.ocpp.backend->chargePointId != point) continue;
        copyOcppRecord(record, false, out);
        payload = record.startRequest;
        found = true;
        break;
    }
    unlock_log();
    return found;
}

bool charge_session_log_confirm_recovered_start(uint32_t id, int32_t transactionId) {
    if (transactionId < 0) return false;
    lock_log();
    bool found = false;
    for (auto& record : s_sessions) {
        if (record.transactionId != id || !record.recoverStart ||
            record.ocpp.transactionId >= 0 || !hasRecoveryBoundaries(record)) continue;
        record.ocpp.transactionId = transactionId;
        record.ocpp.start = session_ocpp_state_t::Confirmed;
        record.ocpp.stop = session_ocpp_state_t::Pending;
        record.recoverStart = false;
        record.recoverStop = true;
        record.startRequest = "";
        save_locked();
        found = true;
        break;
    }
    unlock_log();
    return found;
}

bool charge_session_log_set_recovered_stop(uint32_t id, int32_t transactionId, session_ocpp_state_t state) {
    if (state != session_ocpp_state_t::Pending && state != session_ocpp_state_t::Sent &&
        state != session_ocpp_state_t::Confirmed) return false;
    lock_log();
    bool found = false;
    // Nur abgeschlossene alte Datensaetze; die aktuelle Sitzung bleibt unberuehrt.
    for (auto& record : s_sessions) {
        if (record.transactionId == id && record.recoverStop &&
            record.ocpp.start == session_ocpp_state_t::Confirmed &&
            record.ocpp.transactionId == transactionId && record.ocpp.stop != session_ocpp_state_t::Confirmed) {
            if (record.ocpp.stop != state) {
                record.ocpp.stop = state;
                if (state == session_ocpp_state_t::Confirmed) record.recoverStop = false;
                save_locked();
            }
            found = true;
            break;
        }
    }
    unlock_log();
    return found;
}

bool charge_session_log_set_ocpp(uint32_t id, const session_ocpp_link_t& link) {
    lock_log();
    charge_session_record_t* target = nullptr;
    if (s_sessionActive && s_activeSession.transactionId == id) target = &s_activeSession;
    else for (auto& r : s_sessions) if (r.transactionId == id) { target = &r; break; }
    if (target) {
        const auto previousStop = target->ocpp.stop;
        target->ocpp = storeLink(link);
        if (link.start == session_ocpp_state_t::Confirmed && link.transactionId >= 0)
            target->startRequest = "";
        // CP may have ended the record after OCPP took its copy.
        if (target->ocpp.stop == session_ocpp_state_t::None)
            target->ocpp.stop = previousStop;
        save_locked();
    }
    unlock_log();
    return target != nullptr;
}

bool charge_session_log_flush() {
    if (!s_fileMutex || xSemaphoreTake(s_fileMutex, pdMS_TO_TICKS(20)) != pdTRUE) return false;
    lock_log();
    const bool reserve = s_nextId >= s_reservedUntil || uint32_t(s_reservedUntil - s_nextId) < 128;
    const uint32_t newLimit = std::max(s_reservedUntil, s_nextId) + 1024;
    unlock_log();
    if (reserve && newLimit > s_reservedUntil &&
        preferences.putUInt(NEXT_TRANSACTION_KEY, newLimit) == sizeof(uint32_t)) {
        lock_log(); s_reservedUntil = newLimit; unlock_log();
    }
    lock_log();
    if (s_sessionActive && uint32_t(millis() - s_checkpointMs) >= 30000) {
        s_checkpointMs = millis();
        s_activeSession.stopTime = time(nullptr);
        s_activeSession.durationSeconds = (millis() - s_activeSession.startMillis) / 1000;
        s_activeSession.meterStopValid = readSessionMeter(s_activeSession.meterStopWh);
        s_activeSession.energyWh = std::max(0.0f, s_activeSession.meterStopWh - s_activeSession.meterStartWh);
        save_locked();
    }
    const uint32_t revision = s_revision;
    const size_t count = s_sessions.size();
    const bool active = s_sessionActive;
    const bool unchanged = revision == s_savedRevision;
    unlock_log();
    if (unchanged) { xSemaphoreGive(s_fileMutex); return true; }

    // Write the OTHER slot. Interrupted/partial JSON never replaces the last
    // complete snapshot. No flash operation is performed while holding s_mutex.
    const char* path = s_mainNewest ? SESSION_ALT_PATH : SESSION_LOG_PATH;
    File file = SPIFFS.open(path, FILE_WRITE);
    bool ok = bool(file);
    char header[96];
    const int headerLength = snprintf(header, sizeof(header),
        "{\"format\":2,\"revision\":%lu,\"count\":%u,\"sessions\":[",
        static_cast<unsigned long>(revision), unsigned(count + (active ? 1 : 0)));
    if (ok) ok = headerLength > 0 && size_t(headerLength) < sizeof(header) &&
        file.print(header) == size_t(headerLength);
    for (size_t i = 0; ok && i < count + (active ? 1 : 0); ++i) {
        charge_session_record_t record;
        lock_log();
        ok = s_revision == revision;
        if (ok) record = i < count ? s_sessions[i] : s_activeSession;
        unlock_log();
        if (!ok) break;
        if (i && file.print(',') != 1) { ok = false; break; }
        JsonDocument doc;
        auto array = doc.to<JsonArray>();
        addRecordToJson(array, record, i == count);
        if (doc.overflowed()) { ok = false; break; }
        const size_t expected = measureJson(array[0]);
        ok = serializeJson(array[0], file) == expected;
    }
    lock_log();
    ok = ok && revision == s_revision;
    unlock_log();
    if (ok) ok = file.print("],\"complete\":true}") == strlen("],\"complete\":true}");
    if (file) { file.flush(); file.close(); }
    if (ok) {
        lock_log(); s_savedRevision = revision; s_mainNewest = !s_mainNewest; unlock_log();
    }
    xSemaphoreGive(s_fileMutex);
    return ok;
}

String charge_session_log_to_json()
{
    JsonDocument doc;
    JsonArray sessions = doc["sessions"].to<JsonArray>();

    lock_log();
    doc["sessionStoragePending"] = s_revision != s_savedRevision;
    doc["sessionStorageFull"] = s_storageFull;
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

String charge_session_log_to_json_page(size_t offset, size_t limit, bool appView)
{
    if (limit == 0 || limit > 100) {
        limit = 100;
    }

    JsonDocument doc;
    JsonArray sessions = doc["sessions"].to<JsonArray>();

    lock_log();
    doc["sessionStoragePending"] = s_revision != s_savedRevision;
    doc["sessionStorageFull"] = s_storageFull;
    const size_t total = s_sessions.size() + (s_sessionActive ? 1 : 0);
    if (appView) doc["sessionRevision"] = s_revision;
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
            addRecordToJson(sessions, active, true, appView);
            added++;
        }
        index++;
    }

    for (auto it = s_sessions.rbegin(); it != s_sessions.rend() && added < limit; ++it) {
        if (index >= offset) {
            addRecordToJson(sessions, *it, false, appView);
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
        lock_log();
        const bool valid = recordFromJson(obj, record) && record.transactionId < UINT32_MAX - 1024;
        unlock_log();
        if (!valid) continue;
        // Importierte Historie darf weder Start noch Recovery-Stopp erzeugen.
        record.recoverStop = false;
        record.recoverStart = false;
        record.startRequest = ""; // Import darf auch nach Neustart keinen Start-Replay ausloesen.
        if (record.ocpp.start == session_ocpp_state_t::Pending || record.ocpp.start == session_ocpp_state_t::Sent)
            record.ocpp.start = session_ocpp_state_t::Uncertain;
        if (record.ocpp.stop == session_ocpp_state_t::Pending || record.ocpp.stop == session_ocpp_state_t::Sent)
            record.ocpp.stop = session_ocpp_state_t::Uncertain;
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
        if (s_sessionActive && importedRecord.transactionId == s_activeSession.transactionId) continue;
        auto existing = std::find_if(s_sessions.begin(), s_sessions.end(),
                                     [&](const charge_session_record_t& record) {
                                         return record.transactionId == importedRecord.transactionId;
                                     });
        if (existing != s_sessions.end()) {
            if (existing->ocpp.start == session_ocpp_state_t::None) *existing = importedRecord;
        } else {
            if (s_sessions.size() >= MAX_STORED_SESSIONS) continue;
            s_sessions.push_back(importedRecord);
        }
    }

    std::sort(s_sessions.begin(), s_sessions.end(),
              [](const charge_session_record_t& a, const charge_session_record_t& b) {
                  return a.transactionId < b.transactionId;
              });

    bool ok = save_locked();
    s_nextId = std::max(s_nextId, maxTransactionId + 1);
    unlock_log();
    return ok && charge_session_log_flush();
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
