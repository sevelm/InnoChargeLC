#include "grid_protection.hpp"
#include "AA_globals.h"

static constexpr float GRID_RECONNECT_MIN_PU = 0.90f; //207,0VAC
static constexpr float GRID_RECONNECT_MAX_PU = 1.09f; //250,7VAC
static constexpr float GRID_RECONNECT_MIN_HZ = 49.90f;
static constexpr float GRID_RECONNECT_MAX_HZ = 50.10f;
static constexpr float GRID_MAX_PHASE_IMBALANCE_A = 16.0f;

float grid_protection_get_trip_voltage(uint16_t nominalVoltage, uint8_t undervoltagePercent)
{
    // Berechnet die absolute Unterspannungs-Abschaltgrenze in Volt.
    return nominalVoltage * (undervoltagePercent / 100.0f);
}

float grid_protection_get_min_voltage(float l1Voltage, float l2Voltage, float l3Voltage)
{
    // Ermittelt die kleinste gueltige Phasenspannung; Werte <= 0 gelten als ungueltig.
    float minVoltage = 0.0f;

    if (l1Voltage > 0.0f) minVoltage = l1Voltage;
    if (l2Voltage > 0.0f && (minVoltage == 0.0f || l2Voltage < minVoltage)) minVoltage = l2Voltage;
    if (l3Voltage > 0.0f && (minVoltage == 0.0f || l3Voltage < minVoltage)) minVoltage = l3Voltage;

    return minVoltage;
}

bool grid_protection_reconnect_grid_ok()
{
    // Prueft die aktuell gemessenen Netzwerte fuer die Wiederaufnahme.
    uint16_t nominalVoltage = preferences.getUShort("gridNomVolt", 230);

    if (!grid_protection_frequency_reconnect_ok(sdm.frequency)) return false;
    if (!grid_protection_voltage_reconnect_ok(nominalVoltage, sdm.voltL1)) return false;
    if (!threePhaseActive) return true;

    if (sdm.voltL2 != 0.0f && !grid_protection_voltage_reconnect_ok(nominalVoltage, sdm.voltL2)) return false;
    if (sdm.voltL3 != 0.0f && !grid_protection_voltage_reconnect_ok(nominalVoltage, sdm.voltL3)) return false;

    return true;
}

bool grid_protection_voltage_reconnect_ok(uint16_t nominalVoltage, float voltage)
{
    // Prueft eine Phasenspannung fuer die Wiederaufnahme.
    float minVoltage = nominalVoltage * GRID_RECONNECT_MIN_PU;
    float maxVoltage = nominalVoltage * GRID_RECONNECT_MAX_PU;

    return voltage > 0.0f && voltage >= minVoltage && voltage <= maxVoltage;
}

bool grid_protection_frequency_reconnect_ok(float frequency)
{
    // Prueft die Netzfrequenz fuer die Wiederaufnahme.
    return frequency > GRID_RECONNECT_MIN_HZ && frequency < GRID_RECONNECT_MAX_HZ;
}

bool grid_protection_undervoltage_trip_active()
{
    // Prueft, ob eine gueltige Phase unter der parametrierten Unterspannungsgrenze liegt.
    if (preferences.getUChar("gridProfile", 0) != 1) return false;
    if (!sdm.enable || sdm.error) return false;

    uint16_t nominalVoltage = preferences.getUShort("gridNomVolt", 230);
    uint8_t undervoltagePercent = preferences.getUChar("gridUvPct", 80);
    float tripVoltage = grid_protection_get_trip_voltage(nominalVoltage, undervoltagePercent);

    return (sdm.voltL1 > 0.0f && sdm.voltL1 < tripVoltage) ||
           (sdm.voltL2 != 0.0f && sdm.voltL2 < tripVoltage) ||
           (sdm.voltL3 != 0.0f && sdm.voltL3 < tripVoltage);
}

bool grid_protection_phase_imbalance_active()
{
    // Erkennt eine Schieflast nur bei tatsaechlich geschalteter 3-Phasen-Ladung.
    if (preferences.getUChar("gridProfile", 0) != 1) return false;
    if (!sdm.enable || sdm.error) return false;
    if (!stateRelayL1N || !stateRelayL2L3) return false;
    if (sdm.voltL2 == 0.0f || sdm.voltL3 == 0.0f) return false;

    float minCurrent = sdm.currL1;
    float maxCurrent = sdm.currL1;

    if (sdm.currL2 < minCurrent) minCurrent = sdm.currL2;
    if (sdm.currL3 < minCurrent) minCurrent = sdm.currL3;
    if (sdm.currL2 > maxCurrent) maxCurrent = sdm.currL2;
    if (sdm.currL3 > maxCurrent) maxCurrent = sdm.currL3;

    return (maxCurrent - minCurrent) > GRID_MAX_PHASE_IMBALANCE_A;
}



bool grid_protection_reconnect_delay_required()
{
    // TOR-Wiederaufnahmeverzoegerung nur ohne RFID-Auswertung verwenden.
    if (rfidAuth.required) return false;
    if (preferences.getUChar("gridProfile", 0) != 1) return false;

    return //lastCpStatePersistentBoot == StateB_Connected ||
           lastCpStatePersistentBoot == StateC_Charge ||
           lastCpStatePersistentBoot == StateD_VentCharge;
}
