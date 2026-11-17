#include <ArduinoJson.h>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
using String = std::string;
#include "records.hpp"

static std::vector<charge_session_record_t> s_sessions;
static charge_session_record_t s_activeSession;
static bool s_sessionActive = false, s_storageFull = false;
static uint32_t s_revision = 7, s_savedRevision = 7;
static int lockDepth = 0;
static void lock_log() { ++lockDepth; }
static void unlock_log() { --lockDepth; }
static float currentMeterWh() { return 6500; }
static uint32_t millis() { return 123000; }
static String isoTime(time_t value) { return value ? std::to_string(value) : ""; }
static const char* deliveryState(session_ocpp_state_t state) {
    static const char* values[] = {"none", "pending", "sent", "confirmed", "uncertain", "failed"};
    return values[static_cast<unsigned>(state)];
}
#include "reader.hpp"
struct {
    String getString(const char*, const char*) { return "Garage"; }
} preferences; // Deliberately provides no write methods: the public handler must be read-only.
struct {
    std::map<uint8_t, String> replies;
    void sendTXT(uint8_t client, const String& text) { replies[client] = text; }
} webSocket;
static int fallThrough = 0;
static void handle(JsonDocument& doc, uint8_t num) {
    if (!websocket_public_app_allowed(doc)) return;
    const char* action = doc["action"] | "";
#include "handler.hpp"
    ++fallThrough;
}
static JsonDocument request(uint32_t offset, uint8_t client = 1) {
    JsonDocument doc;
    doc["client"] = "innocharge-public-app";
    doc["action"] = "requestAppSessions";
    doc["offset"] = offset;
    doc["requestId"] = 100U + offset;
    doc["limit"] = 100000U; // The public reader always limits itself to 20 records.
    doc["setWifiEnable"] = true; // Must never reach the legacy setting handlers.
    handle(doc, client);
    JsonDocument result;
    assert(!deserializeJson(result, webSocket.replies[client]));
    return result;
}
int main() {
    for (unsigned id = 1; id <= 45; ++id) {
        charge_session_record_t record;
        record.transactionId = id;
        record.userName = "Test user";
        record.idTag = "RFID42";
        record.startRequest = "SECRET_WIRE_PAYLOAD";
        auto backend = std::make_shared<session_backend_t>();
        backend->url = "SECRET_BACKEND_URL";
        backend->chargePointId = "SECRET_BACKEND_ID";
        record.ocpp.backend = backend;
        s_sessions.push_back(record);
    }
    s_sessionActive = true;
    s_activeSession = s_sessions.back();
    s_activeSession.transactionId = 46;
    s_activeSession.meterStartWh = 4000;
    s_activeSession.startMillis = 100000;

    JsonDocument app;
    const auto appText = charge_session_log_to_json_page(0, 20, true);
    assert(!deserializeJson(app, appText));
    assert(lockDepth == 0);
    assert(app["sessionTotal"] == 46);
    assert(app["sessionRevision"] == 7);
    assert(app["sessions"].size() == 20);
    assert(app["sessions"][0]["transactionId"] == 46);
    assert(app["sessions"][0]["active"] == true);
    assert(app["sessions"][0]["energyWh"] == 2500);
    assert(app["sessions"][0]["durationSeconds"] == 23);
    assert(appText.find("SECRET_") == String::npos);
    assert(appText.find("mailer") == String::npos);
    JsonDocument admin;
    assert(!deserializeJson(admin, charge_session_log_to_json_page(0, 100, false)));
    assert(admin["sessions"][0]["ocppStartRequest"] == "SECRET_WIRE_PAYLOAD");
    assert(admin["sessions"][0]["ocppBackendUrl"] == "SECRET_BACKEND_URL");

    std::set<unsigned> ids;
    for (uint32_t offset = 0; offset < 46; offset += 20) {
        auto response = request(offset);
        assert(response["ok"] == true);
        assert(response["requestId"] == 100U + offset);
        assert(response["type"] == "appSessions");
        assert(response["sessionOffset"] == offset);
        assert(response["sessionLimit"] == 20);
        assert(response["wallboxName"] == "Garage");
        for (JsonObject row : response["sessions"].as<JsonArray>()) {
            assert(ids.insert(row["transactionId"].as<unsigned>()).second);
            assert(!row["ocppStartRequest"].is<const char*>());
            assert(!row["ocppBackendUrl"].is<const char*>());
        }
    }
    assert(ids.size() == 46 && *ids.begin() == 1 && *ids.rbegin() == 46);
    assert(fallThrough == 0);
    auto second = request(40, 2);
    assert(second["sessions"].size() == 6);
    auto first = request(0, 1);
    assert(first["sessionOffset"] == 0);

    JsonDocument invalid;
    invalid["client"] = "innocharge-public-app";
    invalid["action"] = "requestAppSessions";
    invalid["offset"] = -1;
    invalid["requestId"] = 99;
    handle(invalid, 3);
    JsonDocument error;
    assert(!deserializeJson(error, webSocket.replies[3]));
    assert(error["ok"] == false);
    invalid["offset"] = "invalid";
    handle(invalid, 3);
    assert(!deserializeJson(error, webSocket.replies[3]));
    assert(error["ok"] == false);
    invalid["action"] = "saveSessionMailerSettings";
    assert(!websocket_public_app_allowed(invalid));
    invalid["action"] = "subscribeUpdates";
    invalid["page"] = "sessions";
    assert(!websocket_public_app_allowed(invalid));
    invalid["action"] = "requestAppSessions";
    for (const char* action : {"requestAppTime", "setDeviceTime"}) {
        invalid["action"] = action;
        assert(websocket_public_app_allowed(invalid));
    }
    for (const char* action : {"rebootDevice", "saveWifiApSettings", "setWebPassword"}) {
        invalid["action"] = action;
        assert(!websocket_public_app_allowed(invalid));
    }
    invalid["action"] = "requestAppSessions";
    invalid.remove("client");
    assert(!websocket_public_app_allowed(invalid));
    s_sessions.clear(); s_sessionActive = false;
    auto empty = request(0);
    assert(empty["ok"] == true && empty["sessions"].size() == 0 && empty["sessionTotal"] == 0);
    assert(lockDepth == 0 && fallThrough == 0);
    puts("PASS: app-only projection, complete paging, independent clients, bounded requests, admin compatibility and no write fallthrough");
}
