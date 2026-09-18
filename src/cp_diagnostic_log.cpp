#include "cp_diagnostic_log.hpp"

#include <math.h>
#include <vector>

#include "AA_globals.h"
#include "control_pilot.hpp"

namespace {

constexpr size_t LOG_CAPACITY = 300;
constexpr uint32_t SAMPLE_INTERVAL_MS = 100;
constexpr uint32_t HEARTBEAT_INTERVAL_MS = 1000;

enum DiagnosticFlags : uint8_t {
    RelayL1N           = 1U << 0,
    RelayL2L3          = 1U << 1,
    ThreePhase         = 1U << 2,
    SwitchToL1N        = 1U << 3,
    SwitchToL2L3       = 1U << 4,
    RfidRequired       = 1U << 5,
    SessionAuthorized  = 1U << 6,
    PhaseSwitchAllowed = 1U << 7,
};

struct DiagnosticEntry {
    uint32_t elapsedMs;
    int16_t dutySetTenths;
    int16_t dutyActualTenths;
    int16_t targetPowerDeciKw;
    uint8_t cpState;
    uint8_t flags;
};

DiagnosticEntry logEntries[LOG_CAPACITY]{};
size_t writeIndex = 0;
size_t entryCount = 0;
bool recording = false;
uint32_t recordingStartedMs = 0;
uint32_t lastSampleMs = 0;
uint32_t lastStoredMs = 0;
portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;

uint8_t currentFlags()
{
    uint8_t flags = 0;
    if (stateRelayL1N) flags |= RelayL1N;
    if (stateRelayL2L3) flags |= RelayL2L3;
    if (threePhaseActive) flags |= ThreePhase;
    if (switchToL1N) flags |= SwitchToL1N;
    if (switchToL2L3) flags |= SwitchToL2L3;
    if (rfidAuth.required) flags |= RfidRequired;
    if (chargeAuthSession.authorized) flags |= SessionAuthorized;
    if (phaseSwitchAllowed) flags |= PhaseSwitchAllowed;
    return flags;
}

bool valuesChanged(const DiagnosticEntry& previous, const DiagnosticEntry& current)
{
    return previous.dutySetTenths != current.dutySetTenths ||
           previous.dutyActualTenths != current.dutyActualTenths ||
           previous.targetPowerDeciKw != current.targetPowerDeciKw ||
           previous.cpState != current.cpState ||
           previous.flags != current.flags;
}

void resetBuffer(uint32_t now)
{
    writeIndex = 0;
    entryCount = 0;
    recordingStartedMs = now;
    lastSampleMs = 0;
    lastStoredMs = 0;
}

} // namespace

void cp_diagnostic_log_start()
{
    const uint32_t now = millis();
    portENTER_CRITICAL(&logMux);
    resetBuffer(now);
    recording = true;
    portEXIT_CRITICAL(&logMux);
}

void cp_diagnostic_log_stop()
{
    portENTER_CRITICAL(&logMux);
    recording = false;
    portEXIT_CRITICAL(&logMux);
}

void cp_diagnostic_log_clear()
{
    const uint32_t now = millis();
    portENTER_CRITICAL(&logMux);
    resetBuffer(now);
    portEXIT_CRITICAL(&logMux);
}

void cp_diagnostic_log_sample()
{
    const uint32_t now = millis();

    portENTER_CRITICAL(&logMux);
    if (!recording || (lastSampleMs != 0 && now - lastSampleMs < SAMPLE_INTERVAL_MS)) {
        portEXIT_CRITICAL(&logMux);
        return;
    }
    lastSampleMs = now;
    portEXIT_CRITICAL(&logMux);

    DiagnosticEntry entry{};
    entry.dutySetTenths = (int16_t)roundf(setCpDuty * 10.0f);
    entry.dutyActualTenths = (int16_t)roundf(get_control_pilot_duty() * 10.0f);
    entry.targetPowerDeciKw = (int16_t)roundf(g_setChargingPower_kW);
    entry.cpState = (uint8_t)vCurrentCpState.state;
    entry.flags = currentFlags();

    portENTER_CRITICAL(&logMux);
    if (!recording) {
        portEXIT_CRITICAL(&logMux);
        return;
    }

    entry.elapsedMs = now - recordingStartedMs;
    const bool changed = entryCount == 0 ||
                         valuesChanged(logEntries[(writeIndex + LOG_CAPACITY - 1) % LOG_CAPACITY], entry);
    const bool heartbeatDue = entryCount == 0 || now - lastStoredMs >= HEARTBEAT_INTERVAL_MS;

    if (changed || heartbeatDue) {
        logEntries[writeIndex] = entry;
        writeIndex = (writeIndex + 1) % LOG_CAPACITY;
        if (entryCount < LOG_CAPACITY) ++entryCount;
        lastStoredMs = now;
    }
    portEXIT_CRITICAL(&logMux);
}

String cp_diagnostic_log_to_json()
{
    std::vector<DiagnosticEntry> snapshot;
    snapshot.reserve(LOG_CAPACITY);
    bool isRecording = false;

    portENTER_CRITICAL(&logMux);
    isRecording = recording;
    const size_t oldestIndex = (writeIndex + LOG_CAPACITY - entryCount) % LOG_CAPACITY;
    for (size_t i = 0; i < entryCount; ++i) {
        snapshot.push_back(logEntries[(oldestIndex + i) % LOG_CAPACITY]);
    }
    portEXIT_CRITICAL(&logMux);

    String json;
    json.reserve(96 + snapshot.size() * 48);
    json += F("{\"type\":\"cpDiagnosticLog\",\"recording\":");
    json += isRecording ? F("true") : F("false");
    json += F(",\"capacity\":");
    json += String(LOG_CAPACITY);
    json += F(",\"entries\":[");

    for (size_t i = 0; i < snapshot.size(); ++i) {
        if (i > 0) json += ',';
        const DiagnosticEntry& entry = snapshot[i];
        json += '[';
        json += String(entry.elapsedMs);
        json += ',';
        json += String(entry.dutySetTenths);
        json += ',';
        json += String(entry.dutyActualTenths);
        json += ',';
        json += String(entry.cpState);
        json += ',';
        json += String(entry.targetPowerDeciKw);
        json += ',';
        json += String(entry.flags);
        json += ']';
    }

    json += F("]}");
    return json;
}
