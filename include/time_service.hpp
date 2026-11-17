#ifndef TIME_SERVICE_HPP
#define TIME_SERVICE_HPP

#include <Arduino.h>

void time_service_begin();
void time_service_request_sync();
bool time_service_is_valid();
bool time_service_set_epoch(uint32_t epoch);
bool time_service_set_local(const char* localDateTime);
String time_service_local_string();
String time_service_iso_utc_string();

#endif
