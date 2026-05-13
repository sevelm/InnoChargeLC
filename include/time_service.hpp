#ifndef TIME_SERVICE_HPP
#define TIME_SERVICE_HPP

#include <Arduino.h>

void time_service_begin();
void time_service_request_sync();
bool time_service_is_valid();
String time_service_local_string();
String time_service_iso_utc_string();

#endif
