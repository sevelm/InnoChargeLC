#include "A_Task_OCPP.hpp"

#include <math.h>
#include <string.h>

#include "Arduino.h"
#include "MicroOcpp.h"
#include "esp_log.h"
#include "freertos/queue.h"

/*
 * ================= INNOCHARGE EVSE <-> OCPP ADAPTER =================
 *
 * This file is the only place in the firmware allowed to call MicroOCPP.
 * The public interface in A_Task_OCPP.hpp transports copied POD data only.
 * It deliberately does not expose MicroOCPP types, Arduino String instances
 * or pointers owned by CP, RFID, meter, network or UI tasks.
 *
 * Phase 1 (this implementation)
 * --------------------------------
 * - removes the handwritten partial OCPP/WebSocket implementation
 * - compiles the pinned MicroOCPP dependency with the real target toolchain
 * - validates and copies future UI configuration
 * - keeps a coalesced latest-value snapshot plus separate event/command queues
 * - keeps all charging-control and UI modules unchanged
 *
 * Phase 2
 * -------
 * - initialize MicroOCPP after network and UTC are valid
 * - map snapshots to connector, readiness, meter and error callbacks
 * - map RFID/local-stop events to MicroOCPP transactions
 * - expose protocol state and asynchronous remote commands
 *
 * Production blockers are listed next to the public contract in the header.
 * In particular, transaction permission and Smart-Charging limits must be
 * consumed by owner APIs in charging control; writing shared globals here
 * would create races and unsafe last-writer-wins behaviour.
 * =====================================================================
 */

namespace {

constexpr uint8_t OCPP_EVENT_QUEUE_DEPTH = 8;
constexpr uint8_t OCPP_COMMAND_QUEUE_DEPTH = 8;
constexpr TickType_t OCPP_TASK_IDLE_TICKS = pdMS_TO_TICKS(25);
constexpr int64_t EARLIEST_REASONABLE_UTC = 1577836800LL; // 2020-01-01

const char* const TAG = "Task_OCPP";

enum class input_event_type_t : uint8_t {
    IdTagPresented = 0,
    LocalStop,
    CommandFeedback,
};

/* Discrete events never share capacity with the replaceable EVSE snapshot. */
struct input_event_t {
    input_event_type_t type;
    ocpp_stop_reason_t stopReason;
    ocpp_command_feedback_t commandFeedback;
    char idTag[OCPP_ID_TAG_MAX_LEN + 1];
};

portMUX_TYPE s_stateMux = portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t s_eventQueue = nullptr;
QueueHandle_t s_commandQueue = nullptr;

bool s_taskClaimed = false;
bool s_configAvailable = false;
ocpp_config_t s_config{};

bool s_haveInputs = false;
ocpp_evse_inputs_t s_latestInputs{};

ocpp_charge_control_t s_chargeControl{
    false, // authoritative
    false, // transactionActive
    false, // transactionRunning
    false, // chargePermitted
    false, // smartLimitCurrentValid
    false, // smartLimitPowerValid
    false, // smartLimitPhasesValid
    -1.0F,
    -1.0F,
    -1,
};

ocpp_runtime_status_t s_status{
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    ocpp_connection_state_t::Disabled,
    0,
    0,
    0,
    0,
    {},
};

template <size_t N>
bool has_terminator(const char (&value)[N]) {
    return memchr(value, '\0', N) != nullptr;
}

bool starts_with(const char* value, const char* prefix) {
    if (value == nullptr || prefix == nullptr) {
        return false;
    }
    return strncmp(value, prefix, strlen(prefix)) == 0;
}

bool is_printable_ascii(const char* value, bool allowSpaces) {
    if (value == nullptr) {
        return false;
    }

    for (const unsigned char* p =
             reinterpret_cast<const unsigned char*>(value);
         *p != '\0'; ++p) {
        if (*p < 0x20U || *p > 0x7eU || (!allowSpaces && *p == ' ')) {
            return false;
        }
    }
    return true;
}

bool is_valid_endpoint(const char* url, const char* scheme) {
    if (!starts_with(url, scheme) || !is_printable_ascii(url, false) ||
        strchr(url, '@') != nullptr || strchr(url, '#') != nullptr) {
        return false;
    }

    const char* authority = url + strlen(scheme);
    if (*authority == '\0') {
        return false;
    }

    const char* authorityEnd = strpbrk(authority, "/?");
    return authorityEnd == nullptr || authorityEnd != authority;
}

bool is_valid_charge_point_id(const char* id) {
    return id != nullptr && id[0] != '\0' &&
           is_printable_ascii(id, false) && strpbrk(id, "/?#") == nullptr;
}

void copy_text(char* destination, size_t destinationSize, const char* source) {
    if (destination == nullptr || destinationSize == 0) {
        return;
    }

    if (source == nullptr) {
        destination[0] = '\0';
        return;
    }

    const size_t length = strnlen(source, destinationSize - 1);
    memcpy(destination, source, length);
    destination[length] = '\0';
}

void set_last_error_locked(const char* message) {
    copy_text(s_status.lastError, sizeof(s_status.lastError), message);
}

void set_last_error(const char* message) {
    portENTER_CRITICAL(&s_stateMux);
    set_last_error_locked(message);
    portEXIT_CRITICAL(&s_stateMux);
}

void increment_dropped_input() {
    portENTER_CRITICAL(&s_stateMux);
    if (s_status.droppedInputEvents != UINT32_MAX) {
        ++s_status.droppedInputEvents;
    }
    portEXIT_CRITICAL(&s_stateMux);
}

bool validate_config(const ocpp_config_t& config, const char** error) {
    if (!has_terminator(config.backendUrl) ||
        !has_terminator(config.chargePointId) ||
        !has_terminator(config.authorizationKey) ||
        !has_terminator(config.caCertificatePath) ||
        !has_terminator(config.chargePointVendor) ||
        !has_terminator(config.chargePointModel) ||
        !has_terminator(config.chargePointSerialNumber) ||
        !has_terminator(config.firmwareVersion)) {
        *error = "OCPP config contains an unterminated text field";
        return false;
    }

    if (!config.enabled) {
        return true;
    }

    if (!is_valid_charge_point_id(config.chargePointId)) {
        *error = "Charge Point ID is empty or contains invalid characters";
        return false;
    }

    if (config.chargePointVendor[0] == '\0' ||
        config.chargePointModel[0] == '\0' ||
        !is_printable_ascii(config.chargePointVendor, true) ||
        !is_printable_ascii(config.chargePointModel, true) ||
        !is_printable_ascii(config.chargePointSerialNumber, true) ||
        !is_printable_ascii(config.firmwareVersion, true)) {
        *error = "OCPP BootNotification identity is invalid";
        return false;
    }

    switch (config.securityProfile) {
        case ocpp_security_profile_t::TlsBasicAuthentication:
            if (!is_valid_endpoint(config.backendUrl, "wss://")) {
                *error = "Security Profile 2 requires a valid wss:// URL";
                return false;
            }
            if (config.authorizationKey[0] == '\0' ||
                !is_printable_ascii(config.authorizationKey, false)) {
                *error = "Security Profile 2 authorization key is invalid";
                return false;
            }
            if (config.caCertificatePath[0] != '/' ||
                strstr(config.caCertificatePath, "..") != nullptr ||
                !is_printable_ascii(config.caCertificatePath, false)) {
                *error = "Security Profile 2 CA path is invalid";
                return false;
            }
            return true;

        case ocpp_security_profile_t::UnsecuredDevelopment:
            if (!config.allowUnsecuredDevelopment ||
                !is_valid_endpoint(config.backendUrl, "ws://")) {
                *error = "Unsecured OCPP is allowed only for explicit ws:// tests";
                return false;
            }
            return true;

        default:
            *error = "Unsupported OCPP security profile";
            return false;
    }
}

bool validate_inputs(const ocpp_evse_inputs_t& inputs, const char** error) {
    if (!has_terminator(inputs.vendorErrorCode)) {
        *error = "EVSE vendor error code is not terminated";
        return false;
    }

    if (static_cast<uint8_t>(inputs.cpState) >
            static_cast<uint8_t>(ocpp_cp_state_t::Unavailable) ||
        inputs.activePhases > 3U) {
        *error = "EVSE snapshot contains an invalid state or phase count";
        return false;
    }

    constexpr uint32_t allKnownFaults =
        (1UL << (static_cast<uint8_t>(ocpp_fault_code_t::WeakSignal) + 1U)) -
        2U;
    if ((inputs.activeFaultMask & ~allKnownFaults) != 0U) {
        *error = "EVSE snapshot contains an unknown fault bit";
        return false;
    }

    if (inputs.utcTimeValid && inputs.timestampUtc < EARLIEST_REASONABLE_UTC) {
        *error = "EVSE snapshot marks an implausible UTC value as valid";
        return false;
    }

    if (inputs.evReady && !inputs.vehiclePlugged) {
        *error = "EV cannot be ready while the connector is unplugged";
        return false;
    }

    if (inputs.meterValid) {
        if (inputs.energyImportWh < 0 || inputs.energyExportWh < 0 ||
            !isfinite(inputs.activePowerTotalW) ||
            !isfinite(inputs.frequencyHz)) {
            *error = "EVSE snapshot contains an invalid meter total";
            return false;
        }

        for (uint8_t phase = 0; phase < 3; ++phase) {
            if (!isfinite(inputs.activePowerW[phase]) ||
                !isfinite(inputs.voltageV[phase]) ||
                !isfinite(inputs.currentA[phase])) {
                *error = "EVSE snapshot contains an invalid phase value";
                return false;
            }
        }
    }

    return true;
}

bool stack_is_authoritative() {
    bool initialized = false;
    portENTER_CRITICAL(&s_stateMux);
    initialized = s_status.stackInitialized;
    portEXIT_CRITICAL(&s_stateMux);
    return initialized;
}

bool enqueue_event(const input_event_t& event, TickType_t timeoutTicks) {
    QueueHandle_t queue = nullptr;
    portENTER_CRITICAL(&s_stateMux);
    queue = s_eventQueue;
    portEXIT_CRITICAL(&s_stateMux);

    if (queue == nullptr || xQueueSend(queue, &event, timeoutTicks) != pdPASS) {
        increment_dropped_input();
        return false;
    }
    return true;
}

void update_pre_stack_state(bool enabled,
                            ocpp_security_profile_t securityProfile) {
    const uint32_t nowMs = millis();

    portENTER_CRITICAL(&s_stateMux);
    if (s_status.stackInitialized) {
        portEXIT_CRITICAL(&s_stateMux);
        return;
    }

    const bool fresh =
        s_haveInputs &&
        static_cast<uint32_t>(nowMs - s_latestInputs.capturedAtMs) <=
            OCPP_INPUT_STALE_AFTER_MS;
    s_status.networkReady = fresh && s_latestInputs.networkReady;
    s_status.timeValid = fresh && s_latestInputs.utcTimeValid;

    if (!enabled) {
        s_status.connectionState = ocpp_connection_state_t::Disabled;
    } else if (!s_status.networkReady) {
        s_status.connectionState = ocpp_connection_state_t::WaitingForNetwork;
    } else if (securityProfile ==
                   ocpp_security_profile_t::TlsBasicAuthentication &&
               !s_status.timeValid) {
        s_status.connectionState = ocpp_connection_state_t::WaitingForTime;
    } else {
        s_status.connectionState = ocpp_connection_state_t::InterfaceReady;
    }
    portEXIT_CRITICAL(&s_stateMux);
}

void process_unexpected_phase1_event(const input_event_t& /*event*/) {
    increment_dropped_input();
    set_last_error("Unexpected discrete event before OCPP phase 2");
    ESP_LOGE(TAG, "Unexpected discrete event before OCPP phase 2");
}

} // namespace

bool ocpp_set_config(const ocpp_config_t& config) {
    const char* error = nullptr;
    if (!validate_config(config, &error)) {
        set_last_error(error);
        return false;
    }

    portENTER_CRITICAL(&s_stateMux);
    memset(s_config.authorizationKey, 0, sizeof(s_config.authorizationKey));
    s_config = config;
    if (!config.enabled) {
        memset(s_config.authorizationKey, 0,
               sizeof(s_config.authorizationKey));
    }
    s_configAvailable = true;
    set_last_error_locked("");
    if (!config.enabled) {
        s_status.connectionState = ocpp_connection_state_t::Disabled;
    } else if (!s_status.taskInitialized) {
        s_status.connectionState =
            ocpp_connection_state_t::ConfiguredNotStarted;
    }
    portEXIT_CRITICAL(&s_stateMux);
    return true;
}

bool ocpp_publish_evse_inputs(const ocpp_evse_inputs_t& inputs) {
    const char* error = nullptr;
    if (!validate_inputs(inputs, &error)) {
        set_last_error(error);
        return false;
    }

    portENTER_CRITICAL(&s_stateMux);
    s_latestInputs = inputs;
    s_haveInputs = true;
    portEXIT_CRITICAL(&s_stateMux);
    return true;
}

bool ocpp_present_id_tag(const char* idTag, TickType_t timeoutTicks) {
    if (idTag == nullptr) {
        set_last_error("ID tag is null");
        return false;
    }

    const size_t length = strnlen(idTag, OCPP_ID_TAG_MAX_LEN + 1);
    if (length == 0 || length > OCPP_ID_TAG_MAX_LEN ||
        !is_printable_ascii(idTag, false)) {
        set_last_error("ID tag is outside the OCPP 1.6 character/length limit");
        return false;
    }

    if (!stack_is_authoritative()) {
        set_last_error("ID tag rejected because the OCPP stack is not active");
        return false;
    }

    input_event_t event{};
    event.type = input_event_type_t::IdTagPresented;
    memcpy(event.idTag, idTag, length);
    event.idTag[length] = '\0';
    return enqueue_event(event, timeoutTicks);
}

bool ocpp_request_local_stop(ocpp_stop_reason_t reason,
                             TickType_t timeoutTicks) {
    if (static_cast<uint8_t>(reason) >
        static_cast<uint8_t>(ocpp_stop_reason_t::UnlockCommand)) {
        set_last_error("Invalid OCPP stop reason");
        return false;
    }

    if (!stack_is_authoritative()) {
        set_last_error("Local stop rejected because the OCPP stack is not active");
        return false;
    }

    input_event_t event{};
    event.type = input_event_type_t::LocalStop;
    event.stopReason = reason;
    return enqueue_event(event, timeoutTicks);
}

bool ocpp_report_command_feedback(const ocpp_command_feedback_t& feedback,
                                  TickType_t timeoutTicks) {
    const uint8_t result = static_cast<uint8_t>(feedback.result);
    const bool resultValid =
        result <= static_cast<uint8_t>(ocpp_command_result_t::Failed);
    const bool progressValid =
        feedback.progressPercent <= 100U &&
        (feedback.result != ocpp_command_result_t::Succeeded ||
         feedback.progressPercent == 100U) &&
        (feedback.result == ocpp_command_result_t::InProgress ||
         feedback.result == ocpp_command_result_t::Succeeded ||
         feedback.progressPercent == 0U);

    if (feedback.requestId == 0U || !resultValid || !progressValid ||
        !has_terminator(feedback.detail)) {
        set_last_error("Invalid OCPP command feedback");
        return false;
    }

    if (!stack_is_authoritative()) {
        set_last_error(
            "Command feedback rejected because the OCPP stack is not active");
        return false;
    }

    input_event_t event{};
    event.type = input_event_type_t::CommandFeedback;
    event.commandFeedback = feedback;
    return enqueue_event(event, timeoutTicks);
}

bool ocpp_get_charge_control(ocpp_charge_control_t* outControl) {
    if (outControl == nullptr) {
        return false;
    }

    portENTER_CRITICAL(&s_stateMux);
    *outControl = s_chargeControl;
    portEXIT_CRITICAL(&s_stateMux);
    return outControl->authoritative;
}

bool ocpp_take_command(ocpp_command_t* outCommand, TickType_t timeoutTicks) {
    if (outCommand == nullptr) {
        return false;
    }

    QueueHandle_t queue = nullptr;
    portENTER_CRITICAL(&s_stateMux);
    queue = s_commandQueue;
    portEXIT_CRITICAL(&s_stateMux);

    return queue != nullptr &&
           xQueueReceive(queue, outCommand, timeoutTicks) == pdPASS;
}

bool ocpp_get_runtime_status(ocpp_runtime_status_t* outStatus) {
    if (outStatus == nullptr) {
        return false;
    }

    portENTER_CRITICAL(&s_stateMux);
    *outStatus = s_status;
    portEXIT_CRITICAL(&s_stateMux);
    return true;
}

void A_Task_OCPP(void* /*pvParameter*/) {
    portENTER_CRITICAL(&s_stateMux);
    if (s_taskClaimed) {
        portEXIT_CRITICAL(&s_stateMux);
        ESP_LOGE(TAG, "OCPP task may only be started once");
        vTaskDelete(nullptr);
        return;
    }
    s_taskClaimed = true;
    portEXIT_CRITICAL(&s_stateMux);

    QueueHandle_t eventQueue =
        xQueueCreate(OCPP_EVENT_QUEUE_DEPTH, sizeof(input_event_t));
    QueueHandle_t commandQueue =
        xQueueCreate(OCPP_COMMAND_QUEUE_DEPTH, sizeof(ocpp_command_t));

    if (eventQueue == nullptr || commandQueue == nullptr) {
        if (eventQueue != nullptr) {
            vQueueDelete(eventQueue);
        }
        if (commandQueue != nullptr) {
            vQueueDelete(commandQueue);
        }
        portENTER_CRITICAL(&s_stateMux);
        s_taskClaimed = false;
        set_last_error_locked("Unable to allocate OCPP queues");
        s_status.connectionState = ocpp_connection_state_t::Faulted;
        portEXIT_CRITICAL(&s_stateMux);
        ESP_LOGE(TAG, "Unable to allocate OCPP queues");
        vTaskDelete(nullptr);
        return;
    }

    portENTER_CRITICAL(&s_stateMux);
    s_eventQueue = eventQueue;
    s_commandQueue = commandQueue;
    s_status.taskInitialized = true;
    set_last_error_locked("");
    portEXIT_CRITICAL(&s_stateMux);

    ESP_LOGI(TAG, "OCPP adapter phase 1 ready; protocol stack not started");

    while (true) {
        input_event_t event{};
        if (xQueueReceive(eventQueue, &event, OCPP_TASK_IDLE_TICKS) == pdPASS) {
            process_unexpected_phase1_event(event);
        }

        bool configAvailable = false;
        bool enabled = false;
        ocpp_security_profile_t securityProfile =
            ocpp_security_profile_t::UnsecuredDevelopment;

        portENTER_CRITICAL(&s_stateMux);
        configAvailable = s_configAvailable;
        enabled = s_config.enabled;
        securityProfile = s_config.securityProfile;
        portEXIT_CRITICAL(&s_stateMux);

        if (configAvailable) {
            update_pre_stack_state(enabled, securityProfile);
        }

        /*
         * No mocpp_* call belongs in phase 1. Including MicroOcpp.h above
         * deliberately makes PlatformIO compile the pinned library sources;
         * runtime initialization is added only after inputs, persistence and
         * TLS certificate ownership are agreed in phase 2.
         */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
