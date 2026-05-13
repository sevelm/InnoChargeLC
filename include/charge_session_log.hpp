#ifndef CHARGE_SESSION_LOG_HPP
#define CHARGE_SESSION_LOG_HPP

#include <Arduino.h>
#include "AA_globals.h"

void charge_session_log_begin();
void charge_session_log_update(charging_state_t state);
void charge_session_log_set_stop_reason(const char* reason);
String charge_session_log_to_json();
String charge_session_log_to_json_page(size_t offset, size_t limit);
bool charge_session_log_import_json(const String& json);
bool charge_session_log_import_file(const char* path);

#endif
