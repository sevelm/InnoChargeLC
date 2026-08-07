#ifndef GRID_PROTECTION_HPP
#define GRID_PROTECTION_HPP

#include <Arduino.h>

float grid_protection_get_trip_voltage(uint16_t nominalVoltage, uint8_t undervoltagePercent);
float grid_protection_get_min_voltage(float l1Voltage, float l2Voltage, float l3Voltage);
bool grid_protection_voltage_reconnect_ok(uint16_t nominalVoltage, float voltage);
bool grid_protection_frequency_reconnect_ok(float frequency);
bool grid_protection_reconnect_grid_ok();
bool grid_protection_undervoltage_trip_active();
bool grid_protection_phase_imbalance_active();
bool grid_protection_reconnect_delay_required();

#endif
