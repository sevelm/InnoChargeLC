#ifndef A_TASK_OCPP_HPP
#define A_TASK_OCPP_HPP

#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*
 * InnoCharge <-> OCPP module boundary
 * ===================================
 *
 * This header is the single integration contract between the charger firmware
 * and the OCPP stack. MicroOCPP and all OCPP protocol state stay inside
 * A_Task_OCPP.cpp. CP, RFID, meter and UI code must not call MicroOCPP
 * directly.
 *
 * Threading rule
 * --------------
 * Only A_Task_OCPP may execute MicroOCPP functions. Other tasks exchange
 * plain, copied data through the functions below. In particular, Arduino
 * String objects and pointers owned by another task must never cross this
 * boundary.
 *
 * REQUIRED INPUTS FROM THE CHARGER (not connected in phase 1)
 * ----------------------------------------------------------
 * [ ] Debounced CP state / vehicle plugged
 * [ ] EV ready and EVSE ready state
 * [ ] Imported energy in Wh (monotonic transaction meter)
 * [ ] Present voltage, current, active power and frequency
 * [ ] Meter validity and OCPP fault information
 * [ ] RFID/idTag presented and local stop request
 * [ ] Local availability (maintenance / grid protection)
 * [ ] Reliable UTC time and network availability
 *
 * REQUIRED OUTPUTS TO THE CHARGER (not consumed in phase 1)
 * --------------------------------------------------------
 * [ ] OCPP transaction authorization / permission to charge
 * [ ] Smart-Charging limit in A and/or W plus requested phase count
 * [ ] Remote reset request
 * [ ] UnlockConnector request and completion result
 * [ ] Firmware update request and progress result
 * [ ] Diagnostics upload request and progress result
 *
 * OWNER INTERFACES STILL REQUIRED FOR PRODUCTION
 * ------------------------------------------------
 * [ ] Charging-control-owned authorization gate. OCPP must not write the
 *     RFID globals directly or race the local authorization state.
 * [ ] Charging-control-owned limit arbiter. The effective limit must be the
 *     safe minimum of hardware, grid, local, Modbus/DPL and OCPP limits.
 * [ ] Asynchronous reset, connector-lock, firmware and diagnostics services
 *     with completion feedback. OCPP callbacks must never block.
 *
 * Until those owner interfaces exist, RemoteStart/RemoteStop,
 * ChangeAvailability, Smart Charging, UnlockConnector, Reset, UpdateFirmware
 * and GetDiagnostics must not be advertised as product-ready.
 *
 * REQUIRED UI CONFIGURATION (UI MUST NOT be changed before its own plan)
 * ----------------------------------------------------------------------
 * [ ] OCPP enabled
 * [ ] Backend WSS URL
 * [ ] Charge Point ID
 * [ ] Authorization key (write-only; never return it to the browser)
 * [ ] Security Profile 2 for production; Profile 0 only for local tests
 * [ ] Trusted CSMS CA certificate upload / replacement
 * [ ] Read-only connection, BootNotification, transaction and TLS status
 *
 * Production target
 * -----------------
 * OCPP 1.6-J, current applicable errata, Security Profile 2 (TLS 1.2+
 * and HTTP Basic authentication), Core and Smart Charging. A backend's PICS
 * and integration guide remain authoritative for optional features.
 */

constexpr uint8_t OCPP_CONNECTOR_ID = 1;
constexpr uint32_t OCPP_INPUT_STALE_AFTER_MS = 5000;
constexpr size_t OCPP_BACKEND_URL_MAX_LEN = 256;
constexpr size_t OCPP_CHARGE_POINT_ID_MAX_LEN = 64;
constexpr size_t OCPP_AUTH_KEY_MAX_LEN = 96;
constexpr size_t OCPP_CERT_PATH_MAX_LEN = 64;
constexpr size_t OCPP_ID_TAG_MAX_LEN = 20; // OCPP 1.6 idTag schema limit
constexpr size_t OCPP_VENDOR_MAX_LEN = 20;
constexpr size_t OCPP_MODEL_MAX_LEN = 20;
constexpr size_t OCPP_SERIAL_MAX_LEN = 25;
constexpr size_t OCPP_FIRMWARE_VERSION_MAX_LEN = 50;
constexpr size_t OCPP_REMOTE_LOCATION_MAX_LEN = 512;
constexpr size_t OCPP_TEXT_MAX_LEN = 96;

enum class ocpp_security_profile_t : uint8_t {
    UnsecuredDevelopment = 0,
    TlsBasicAuthentication = 2,
};

enum class ocpp_cp_state_t : uint8_t {
    Unknown = 0,
    A_NotConnected,
    B_Connected,
    C_Charging,
    D_VentilationRequired,
    E_Error,
    F_Fault,
    Unavailable,
};

enum class ocpp_fault_code_t : uint8_t {
    NoError = 0,
    ConnectorLockFailure,
    EVCommunicationError,
    GroundFailure,
    HighTemperature,
    InternalError,
    LocalListConflict,
    OtherError,
    OverCurrentFailure,
    OverVoltage,
    PowerMeterFailure,
    PowerSwitchFailure,
    ReaderFailure,
    ResetFailure,
    UnderVoltage,
    WeakSignal,
};

enum class ocpp_stop_reason_t : uint8_t {
    Local = 0,
    EVDisconnected,
    DeAuthorized,
    EmergencyStop,
    HardReset,
    Other,
    PowerLoss,
    Reboot,
    Remote,
    SoftReset,
    UnlockCommand,
};

enum class ocpp_connection_state_t : uint8_t {
    Disabled = 0,
    ConfiguredNotStarted,
    InterfaceReady,
    WaitingForNetwork,
    WaitingForTime,
    Initializing,
    Connecting,
    Connected,
    BootPending,
    Ready,
    Faulted,
};

/*
 * Configuration is supplied by the future UI/persistence adapter. Calling
 * ocpp_set_config() does not persist secrets. The authorization key must be
 * stored write-only and protected by the platform security configuration.
 */
struct ocpp_config_t {
    bool enabled;
    ocpp_security_profile_t securityProfile;
    bool allowUnsecuredDevelopment;

    char backendUrl[OCPP_BACKEND_URL_MAX_LEN];
    char chargePointId[OCPP_CHARGE_POINT_ID_MAX_LEN];
    char authorizationKey[OCPP_AUTH_KEY_MAX_LEN];
    char caCertificatePath[OCPP_CERT_PATH_MAX_LEN];

    char chargePointVendor[OCPP_VENDOR_MAX_LEN + 1];
    char chargePointModel[OCPP_MODEL_MAX_LEN + 1];
    char chargePointSerialNumber[OCPP_SERIAL_MAX_LEN + 1];
    char firmwareVersion[OCPP_FIRMWARE_VERSION_MAX_LEN + 1];
};

/*
 * One coherent charger snapshot. Units are deliberately explicit because
 * OCPP distinguishes W/kW, A, V and Wh/kWh.
 */
struct ocpp_evse_inputs_t {
    uint32_t sampleSequence;
    uint32_t capturedAtMs; // monotonic millis(); freshness / publisher watchdog
    int64_t timestampUtc;  // whole seconds since Unix epoch, never milliseconds

    bool networkReady;
    bool utcTimeValid;

    ocpp_cp_state_t cpState;
    bool vehiclePlugged;
    bool evReady;
    bool evseReady;
    bool locallyOperative;

    bool meterValid;
    int64_t energyImportWh;
    int64_t energyExportWh;
    float activePowerTotalW;
    float activePowerW[3];
    float voltageV[3];
    float currentA[3];
    float frequencyHz;
    uint8_t activePhases;

    /* Multiple simultaneous faults are represented as enum-bit positions. */
    uint32_t activeFaultMask;
    char vendorErrorCode[32];
};

constexpr uint32_t ocpp_fault_bit(ocpp_fault_code_t fault) {
    return fault == ocpp_fault_code_t::NoError
               ? 0U
               : (1UL << static_cast<uint8_t>(fault));
}

/*
 * Result that charging control must eventually consume. `authoritative` is
 * false until the OCPP stack owns a valid result; consumers must then ignore
 * every other field. Active means that a transaction attempt/object exists;
 * running means that its OCPP StartTransaction phase has begun successfully.
 *
 * Current and power limits have independent validity flags. If both are
 * valid, the charging-control-owned arbiter must enforce both constraints
 * together with all non-OCPP safety and local limits.
 */
struct ocpp_charge_control_t {
    bool authoritative;
    bool transactionActive;
    bool transactionRunning;
    bool chargePermitted;
    bool smartLimitCurrentValid;
    bool smartLimitPowerValid;
    bool smartLimitPhasesValid;
    float smartLimitCurrentA;
    float smartLimitPowerW;
    int8_t smartLimitPhases;
};

enum class ocpp_command_type_t : uint8_t {
    None = 0,
    Reset,
    UnlockConnector,
    ChangeAvailability,
    FirmwareUpdate,
    DiagnosticsUpload,
};

/*
 * Commands are copied out of the OCPP task. URL/path fields are intentionally
 * bounded so the queue never contains borrowed pointers.
 */
struct ocpp_command_t {
    ocpp_command_type_t type;
    uint32_t requestId;
    uint8_t connectorId;

    bool hardReset;
    bool operative;

    /* UpdateFirmware: UTC seconds; -1 means that an optional value is absent. */
    int64_t retrieveDateUtc;
    int32_t retries;
    int32_t retryIntervalSeconds;

    /* GetDiagnostics: UTC seconds; 0 means that a bound is absent. */
    int64_t diagnosticsStartTimeUtc;
    int64_t diagnosticsStopTimeUtc;

    char location[OCPP_REMOTE_LOCATION_MAX_LEN + 1];
};

enum class ocpp_command_result_t : uint8_t {
    Accepted = 0,
    Rejected,
    NotSupported,
    InProgress,
    Succeeded,
    Failed,
};

/* Charger -> OCPP completion feedback for asynchronous remote commands. */
struct ocpp_command_feedback_t {
    uint32_t requestId;
    ocpp_command_result_t result;
    uint8_t progressPercent;
    char detail[64];
};

/* Read-only values intended for diagnostics and the future UI. */
struct ocpp_runtime_status_t {
    bool taskInitialized;
    bool stackInitialized;
    bool networkReady;
    bool timeValid;
    bool websocketConnected;
    bool bootAccepted;
    bool transactionActive;
    bool transactionRunning;
    bool chargePermitted;

    ocpp_connection_state_t connectionState;
    uint32_t reconnectCount;
    uint32_t droppedInputEvents;
    uint32_t droppedCommands;
    uint32_t lastMessageTimeMs;
    char lastError[OCPP_TEXT_MAX_LEN];
};

/* FreeRTOS task entry. It will be started from main only in a later phase. */
void A_Task_OCPP(void* pvParameter);

/* Configuration hand-off. Safe before or after task creation; not persistent. */
bool ocpp_set_config(const ocpp_config_t& config);

/* Charger -> OCPP inputs. Each call copies its argument. */
bool ocpp_publish_evse_inputs(const ocpp_evse_inputs_t& inputs);
bool ocpp_present_id_tag(const char* idTag,
                         TickType_t timeoutTicks = pdMS_TO_TICKS(10));
bool ocpp_request_local_stop(ocpp_stop_reason_t reason,
                             TickType_t timeoutTicks = pdMS_TO_TICKS(10));
bool ocpp_report_command_feedback(
    const ocpp_command_feedback_t& feedback,
    TickType_t timeoutTicks = pdMS_TO_TICKS(10));

/* OCPP -> charger outputs. These remain unconsumed until the adapter is wired. */
bool ocpp_get_charge_control(ocpp_charge_control_t* outControl);
bool ocpp_take_command(ocpp_command_t* outCommand,
                       TickType_t timeoutTicks = 0);

/* Read-only monitoring for logs and the future UI. */
bool ocpp_get_runtime_status(ocpp_runtime_status_t* outStatus);

#endif // A_TASK_OCPP_HPP
