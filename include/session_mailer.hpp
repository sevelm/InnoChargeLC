#ifndef SESSION_MAILER_HPP
#define SESSION_MAILER_HPP

#include <Arduino.h>
#include <ArduinoJson.h>

void session_mailer_begin();
void session_mailer_append_status(JsonObject root);
void session_mailer_save_settings(JsonObjectConst settings);
bool session_mailer_send_manual_report();
void session_mailer_run_automatic();

#endif
