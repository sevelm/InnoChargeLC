#ifndef CHARGE_SESSION_LOG_HPP
#define CHARGE_SESSION_LOG_HPP

#include <Arduino.h>
#include "AA_globals.h"

enum class session_ocpp_state_t : uint8_t { None, Pending, Sent, Confirmed, Uncertain, Failed };
struct session_ocpp_link_t {
    int32_t transactionId = -1;
    session_ocpp_state_t start = session_ocpp_state_t::None;
    session_ocpp_state_t stop = session_ocpp_state_t::None;
    bool local = false;
    char idTag[21]{};
    char backendUrl[256]{};
    char chargePointId[64]{};
};
// Copied record for the OCPP task, never a reference into the session vector.
struct session_ocpp_record_t {
    uint32_t localId = 0;
    uint32_t authorizationStamp = 0;
    bool active = false;
    bool meterStartValid = false;
    bool meterStopValid = false;
    int32_t meterStartWh = 0;
    int32_t meterStopWh = 0;
    time_t startTime = 0;
    time_t stopTime = 0;
    char stopReason[16]{};
    session_ocpp_link_t ocpp;
};
void charge_session_log_prepare_ocpp(uint32_t stamp, const session_ocpp_link_t& link);
bool charge_session_log_get_ocpp(uint32_t localId, uint32_t stamp, session_ocpp_record_t& record);
bool charge_session_log_next_ocpp(const char* backendUrl, const char* chargePointId, session_ocpp_record_t& record);
bool charge_session_log_set_ocpp(uint32_t localId, const session_ocpp_link_t& link);
// Original-Start vor dem ersten Senden sichern; kein nachtraegliches Rekonstruieren.
bool charge_session_log_capture_start(uint32_t localId, const String& payload);
bool charge_session_log_next_recovered_start(const char* backendUrl, const char* chargePointId,
    session_ocpp_record_t& record, String& payload);
bool charge_session_log_confirm_recovered_start(uint32_t localId, int32_t transactionId);
// Bekannte alte ID: denselben vorhandenen Stopp-Nachversand weiterverwenden.
bool charge_session_log_next_recovered_stop(const char* backendUrl, const char* chargePointId, session_ocpp_record_t& record);
bool charge_session_log_set_recovered_stop(uint32_t localId, int32_t transactionId, session_ocpp_state_t state);
bool charge_session_log_flush();
bool charge_session_log_has_active_session();

void charge_session_log_begin();
void charge_session_log_update(charging_state_t state);
void charge_session_log_set_stop_reason(const char* reason);
String charge_session_log_to_json();
String charge_session_log_to_json_page(size_t offset, size_t limit, bool appView = false);
bool charge_session_log_import_json(const String& json);
bool charge_session_log_import_file(const char* path);

#endif
