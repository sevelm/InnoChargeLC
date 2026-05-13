#ifndef RFID_DB_HPP
#define RFID_DB_HPP

#include <Arduino.h>
#include <vector>

struct rfid_user_t {
    String idTag;
    String name;
    bool enabled;
    uint16_t maxChargeMinutes;
    String note;
};

void rfid_db_begin();
String rfid_db_normalize_id_tag(const String& idTag);
bool rfid_db_upsert_user(const rfid_user_t& user);
bool rfid_db_delete_user(const String& idTag);
bool rfid_db_find_user(const String& idTag, rfid_user_t* outUser);
bool rfid_db_is_authorized(const String& idTag, rfid_user_t* outUser = nullptr);
String rfid_db_to_json();

#endif
