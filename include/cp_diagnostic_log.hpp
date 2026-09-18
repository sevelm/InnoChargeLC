#pragma once

#include <Arduino.h>

void cp_diagnostic_log_start();
void cp_diagnostic_log_stop();
void cp_diagnostic_log_clear();
void cp_diagnostic_log_sample();
String cp_diagnostic_log_to_json();
