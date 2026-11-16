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
 * STATUS / METER INTEGRATION
 * - The original local RFID session controls charging.
 * - OCPP reads existing CP state and debounces status/error together (250 ms).
 * - Modbus publishes one completed meter cycle; OCPP checks freshness/units.
 * - OCPP grants/stops use the existing MB-owned authorization session.
 * - Local RFID grants immediately; optional reporting never gates that grant.
 * - Local/backend IDs and delivery states are stored in the existing session log.
 * - Reboot: replay only a durably saved original Start, then close the returned ID.
 * - Older ambiguous starts without an original payload still require review.
 *
 * REMAINING / EXTENDED INPUT CONTRACT
 * ----------------------------------------------------------
 * [x] Debounced CP state / vehicle plugged
 * [x] EV ready and EVSE ready state (read-only status mapping)
 * [x] Imported energy in Wh and active import power in W
 * [ ] Present voltage, current, active power and frequency
 * [x] Meter validity and OCPP fault information
 * [x] RFID authorization and backend stop through existing session functions
 * [x] Backend availability gate (local RFID deliberately remains independent)
 * [ ] Station-wide maintenance / grid-protection availability
 * [x] Network availability and system UTC (connection test only)
 *
 * OUTPUTS TO THE CHARGER
 * --------------------------------------------------------
 * [x] OCPP authorization / stop commands consumed by MB (not a second CP gate)
 * [ ] Smart-Charging limit in A and/or W plus requested phase count
 * [x] Remote Soft/Hard reset: owner shutdown, durable session close, then reboot
 * [ ] UnlockConnector request and completion result
 * [ ] Firmware update request and progress result
 * [ ] Diagnostics upload request and progress result
 *
 * OWNER INTERFACES STILL REQUIRED FOR PRODUCTION
 * ------------------------------------------------
 * [x] Reuse existing authorization session; OCPP never writes RFID globals.
 * [ ] Charging-control-owned limit arbiter. The effective limit must be the
 *     safe minimum of hardware, grid, local, Modbus/DPL and OCPP limits.
 * [x] Asynchronous reset preparation with owner feedback and storage/output checks
 * [ ] Asynchronous connector-lock, firmware and diagnostics services
 *     with completion feedback. OCPP callbacks must never block.
 *
 * RemoteStart/RemoteStop use the existing session with RFID required ON.
 * ChangeAvailability gates NEW OCPP grants/reporting only (intentional local-use policy).
 * ChangeConfiguration permits HeartbeatInterval / MeterValueSampleInterval
 * and TransactionMessageAttempts / TransactionMessageRetryInterval
 * and InnoChargeMaxChargingPowerKW (existing persisted station cap, not a profile).
 * Smart Charging, UnlockConnector, UpdateFirmware
 * and GetDiagnostics remain blocked.
 *
 * UI CONFIGURATION (connection-test UI implemented; production items open)
 * ----------------------------------------------------------------------
 * [x] OCPP enabled, persisted, with runtime stop/restart
 * [x] Backend WSS URL
 * [x] Charge Point ID and station identity
 * [x] Authorization key (write-only UI; NVS is not encrypted)
 * [x] Security Profile 2 transport; Profile 0 retained for explicit tests
 * [x] Trusted CSMS CA certificate upload / replacement (NVS)
 * [x] Connection / BootNotification / heartbeat status and bounded event log
 * [x] TLS verification and device-time status
 * [x] Runtime transaction status and persistent Sessions linkage / delivery states
 *
 * Production target
 * -----------------
 * OCPP 1.6-J, current applicable errata, Security Profile 2 (TLS 1.2+
 * and HTTP Basic authentication), Core and Smart Charging. A backend's PICS
 * and integration guide remain authoritative for optional features.
 */

constexpr uint8_t OCPP_CONNECTOR_ID = 1;

// One completed meter cycle. The lock protects only this small data copy,
// never Modbus I/O, CP processing or the charging sequence.
struct ocpp_meter_sample_t {
    bool valid;
    uint32_t capturedAtMs;
    float importKWh;
    float powerW;
};
extern portMUX_TYPE ocppMeterMux;
extern ocpp_meter_sample_t ocppMeterSample;

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
    BootRejected,
    Ready,
    Faulted,
};

/*
 * Configuration is supplied by the UI/persistence adapter. Calling
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
    GrantSession,
    StopSession,
    Reset,
    UnlockConnector,
    ChangeAvailability,
    FirmwareUpdate,
    DiagnosticsUpload,
    SetMaxChargingPower,
};

/*
 * Commands are copied out of the OCPP task. URL/path fields are intentionally
 * bounded so the queue never contains borrowed pointers.
 */
struct ocpp_command_t {
    ocpp_command_type_t type;
    uint32_t requestId;
    uint8_t connectorId;

    // Existing MB-owned authorization session; no CP/relay commands.
    uint32_t sessionStamp;
    uint32_t expiresAtMs;
    bool remoteStart;
    bool vehicleConnectedAtRequest;
    char idTag[OCPP_ID_TAG_MAX_LEN + 1];
    char stopReason[16];

    bool hardReset;
    bool operative;
    uint16_t maxChargingPowerTenths; // existing station cap, 0.1 kW

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
    ocpp_command_type_t type;
    uint32_t sessionStamp;
    ocpp_command_result_t result;
    uint8_t progressPercent;
    char detail[64];
};

/* Read-only values intended for diagnostics and the future UI. */
struct ocpp_runtime_status_t {
    bool enabled; // configured ON, also while disconnected / waiting for boot
    bool rfidRejected; // explicit Authorize.conf rejection, never timeout/offline
    char rfidRejectedTag[OCPP_ID_TAG_MAX_LEN + 1];
    bool taskInitialized;
    bool stackInitialized;
    bool networkReady;
    bool timeValid;
    bool websocketConnected;
    bool tlsVerified;
    bool bootAccepted;
    bool backendOperative = true; // Operator policy: OCPP grants blocked, local RFID stays independent.
    uint32_t heartbeatIntervalSeconds;
    uint32_t meterIntervalSeconds;
    bool transactionActive;
    bool transactionRunning;
    bool chargePermitted;

    ocpp_connection_state_t connectionState;
    uint32_t reconnectCount;
    uint32_t droppedInputEvents;
    uint32_t droppedCommands;
    uint32_t lastMessageTimeMs;
    uint32_t heartbeatConfirmations;
    uint32_t lastHeartbeatTimeMs;
    char lastError[OCPP_TEXT_MAX_LEN];
};

/* Starts the controller with saved settings (disabled on first boot).
 * A disabled controller waits without polling; stack/socket are released. */
bool ocpp_start();
void ocpp_local_settings_begin();
bool ocpp_local_rfid_enabled();
bool ocpp_backend_accepts_cards();
void ocpp_prepare_session(uint32_t stamp, const char* normalizedTag, bool backendAuthorized);

/* FreeRTOS task entry. All MicroOCPP functions run in this task. */
void A_Task_OCPP(void* pvParameter);

/* Asynchronous configuration hand-off. The OCPP task alone stops/restarts
 * the stack. Persistence and the imported CA are handled by the Web adapter. */
bool ocpp_set_config(const ocpp_config_t& config);

/* New RFID requests need an online, boot-accepted backend. Local grants do not. */
bool ocpp_present_id_tag(const char* idTag,
                         TickType_t timeoutTicks = pdMS_TO_TICKS(10));
bool ocpp_request_local_stop(ocpp_stop_reason_t reason,
                             TickType_t timeoutTicks = pdMS_TO_TICKS(10));
bool ocpp_report_command_feedback(
    const ocpp_command_feedback_t& feedback,
    TickType_t timeoutTicks = pdMS_TO_TICKS(10));

/* OCPP -> MB commands. Charge-control snapshot is reserved; never a CP gate. */
bool ocpp_get_charge_control(ocpp_charge_control_t* outControl);
bool ocpp_take_command(ocpp_command_t* outCommand,
                       TickType_t timeoutTicks = 0);

/* Read-only monitoring for logs and the future UI. */
bool ocpp_get_runtime_status(ocpp_runtime_status_t* outStatus);

#endif // A_TASK_OCPP_HPP
