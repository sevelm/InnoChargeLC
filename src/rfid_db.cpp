#include "rfid_db.hpp"

#include <ArduinoJson.h>
#include "AA_globals.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static constexpr const char* RFID_DB_KEY = "rfidUsers";

static std::vector<rfid_user_t> s_users;
static SemaphoreHandle_t s_mutex = nullptr;

static void lock_db()
{
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
}

static void unlock_db()
{
    if (s_mutex) xSemaphoreGive(s_mutex);
}

String rfid_db_normalize_id_tag(const String& idTag)
{
    String out;
    out.reserve(idTag.length());
    for (size_t i = 0; i < idTag.length(); ++i) {
        char c = idTag.charAt(i);
        if (c == ':' || c == '-' || c == ' ') continue;
        out += (char)toupper((unsigned char)c);
    }
    return out;
}

static bool save_locked()
{
    JsonDocument doc;
    JsonArray users = doc["users"].to<JsonArray>();
    for (const auto& user : s_users) {
        JsonObject obj = users.add<JsonObject>();
        obj["idTag"] = user.idTag;
        obj["name"] = user.name;
        obj["enabled"] = user.enabled;
        obj["maxChargeMinutes"] = user.maxChargeMinutes;
        obj["note"] = user.note;
    }

    String out;
    serializeJson(doc, out);
    return preferences.putString(RFID_DB_KEY, out) == out.length();
}

static bool load_locked()
{
    s_users.clear();
    String stored = preferences.getString(RFID_DB_KEY, "");
    if (stored.length() == 0) return true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, stored);
    if (err) return false;

    JsonArray users = doc["users"].as<JsonArray>();
    for (JsonObject obj : users) {
        rfid_user_t user;
        user.idTag = rfid_db_normalize_id_tag(obj["idTag"] | "");
        user.name = obj["name"] | "";
        user.enabled = obj["enabled"] | true;
        int maxChargeMinutes = obj["maxChargeMinutes"] | 0;
        if (maxChargeMinutes < 0) maxChargeMinutes = 0;
        if (maxChargeMinutes > 65535) maxChargeMinutes = 65535;
        user.maxChargeMinutes = (uint16_t)maxChargeMinutes;
        user.note = obj["note"] | "";
        if (user.idTag.length() > 0) s_users.push_back(user);
    }
    return true;
}

void rfid_db_begin()
{
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    lock_db();
    load_locked();
    unlock_db();
}

bool rfid_db_upsert_user(const rfid_user_t& userIn)
{
    rfid_user_t user = userIn;
    user.idTag = rfid_db_normalize_id_tag(user.idTag);
    if (user.idTag.length() == 0) return false;

    lock_db();
    for (auto& existing : s_users) {
        if (existing.idTag == user.idTag) {
            existing = user;
            bool ok = save_locked();
            unlock_db();
            return ok;
        }
    }

    s_users.push_back(user);
    bool ok = save_locked();
    unlock_db();
    return ok;
}

bool rfid_db_delete_user(const String& idTag)
{
    String normalized = rfid_db_normalize_id_tag(idTag);
    lock_db();
    for (auto it = s_users.begin(); it != s_users.end(); ++it) {
        if (it->idTag == normalized) {
            s_users.erase(it);
            bool ok = save_locked();
            unlock_db();
            return ok;
        }
    }
    unlock_db();
    return false;
}

bool rfid_db_find_user(const String& idTag, rfid_user_t* outUser)
{
    String normalized = rfid_db_normalize_id_tag(idTag);
    lock_db();
    for (const auto& user : s_users) {
        if (user.idTag == normalized) {
            if (outUser) *outUser = user;
            unlock_db();
            return true;
        }
    }
    unlock_db();
    return false;
}

bool rfid_db_is_authorized(const String& idTag, rfid_user_t* outUser)
{
    rfid_user_t user;
    if (!rfid_db_find_user(idTag, &user)) return false;
    if (outUser) *outUser = user;
    return user.enabled;
}

String rfid_db_to_json()
{
    JsonDocument doc;
    JsonArray users = doc["users"].to<JsonArray>();

    lock_db();
    for (const auto& user : s_users) {
        JsonObject obj = users.add<JsonObject>();
        obj["idTag"] = user.idTag;
        obj["name"] = user.name;
        obj["enabled"] = user.enabled;
        obj["maxChargeMinutes"] = user.maxChargeMinutes;
        obj["note"] = user.note;
    }
    unlock_db();

    String out;
    serializeJson(doc, out);
    return out;
}
