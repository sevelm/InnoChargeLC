#include "A_Task_OCPP.hpp"
#include "ocpp_ui.hpp"
#include "AA_globals.h"
#include "charge_session_log.hpp"
#include "control_pilot.hpp"
#include "ocpp_policy.hpp"
#include "ocpp_auth.hpp"
#include <Preferences.h>
#include <stdarg.h>
#include <memory>
#include <new>
#include <cmath>
#include <mbedtls/x509_crt.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/base64.h>
#include <lwip/netdb.h>

#include <string.h>

#include "Arduino.h"
#include "MicroOcpp.h"
#include "MicroOcpp/Core/Context.h"
#include "MicroOcpp/Core/Operation.h"
#include "MicroOcpp/Core/Configuration.h"
#include "MicroOcpp/Core/Request.h"
#include "MicroOcpp/Model/ConnectorBase/ChargePointStatus.h"
#include "MicroOcpp/Operations/Heartbeat.h"
#include "MicroOcpp/Operations/StatusNotification.h"
#include "MicroOcpp/Operations/CustomOperation.h"
#include "MicroOcpp/Operations/TriggerMessage.h"
#include "MicroOcpp/Model/Authorization/AuthorizationService.h"
#include "MicroOcpp/Model/Reservation/ReservationService.h"
#include "MicroOcpp/Model/Transactions/Transaction.h"
#include "ethernet_manager.hpp"
#include "wifi_manager.hpp"
#include "time_service.hpp"
#include <WiFi.h>
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
 * Connection and authorization integration test
 * --------------------------------
 * - removes the handwritten partial OCPP/WebSocket implementation
 * - runs MicroOCPP BootNotification / Heartbeat with the test backend
 * - validates and copies persisted UI configuration, including SP2 trust data
 * - grants/revokes only through copied commands to the existing MB session
 *
 * Current hardware integration / remaining scope
 * -------
 * - sends debounced CP status and valid, fresh meter samples
 * - local transactions report the selected tag, or remain explicitly private
 * - RemoteStart/Stop reuse that session; no second physical permission gate
 * - known interrupted IDs recover their stop; stored original starts can recover a missing ID
 *
 * Production blockers are listed next to the public contract in the header.
 * In particular, transaction permission and Smart-Charging limits must be
 * consumed by owner APIs in charging control; writing shared globals here
 * would create races and unsafe last-writer-wins behaviour.
 * =====================================================================
 */

portMUX_TYPE ocppMeterMux = portMUX_INITIALIZER_UNLOCKED;
ocpp_meter_sample_t ocppMeterSample{};

namespace {

constexpr uint8_t OCPP_EVENT_QUEUE_DEPTH = 8;
constexpr uint8_t OCPP_COMMAND_QUEUE_DEPTH = 8;
constexpr TickType_t OCPP_TASK_IDLE_TICKS = pdMS_TO_TICKS(25);

// ================= OCPP UI DEFAULTS (DISABLED) =================
// No server is selected on first boot. Saved connection settings take precedence.
constexpr const char* DEFAULT_CHARGE_POINT_ID = "IC000001";
constexpr uint32_t OCPP_TASK_STACK_BYTES = 12288;
// ====================================================================

const char* const TAG = "Task_OCPP";

enum class input_event_type_t : uint8_t {
    IdTagPresented = 0,
    LocalStop,
    CommandFeedback,
};

/* Discrete events never share capacity with the replaceable EVSE snapshot. */
struct input_event_t {
    input_event_type_t type;
    uint32_t createdAtMs;
    uint32_t rfidRequestId; // feedback identity only; never a charging permission
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
TaskHandle_t s_taskHandle = nullptr;
uint32_t s_configRevision = 0;
uint32_t s_appliedRevision = 0;
bool s_authorizationBusy = false; // protects in-flight authorization/transaction from reconfiguration
bool s_configSaving = false;
bool s_resetPending = false; // protected by s_stateMux; blocks new grants/config changes
bool s_backendOperative = true; // protected by s_stateMux; never bypasses local RFID
struct BackendSettingsV1 {
    uint32_t magic = 0x4F435301;
    uint32_t heartbeatSeconds = 60;
    uint32_t meterSeconds = 60;
    uint8_t operative = 1;
    char backendUrl[OCPP_BACKEND_URL_MAX_LEN + 1]{};
    char chargePointId[OCPP_CHARGE_POINT_ID_MAX_LEN + 1]{};
};
struct BackendSettings : BackendSettingsV1 {
    uint32_t transactionMessageAttempts = 3;
    uint32_t transactionMessageRetrySeconds = 60;
} backendSettings; // OCPP task only; separate atomic NVS blob, bound to backend identity
// Remaining reset state is owned exclusively by A_Task_OCPP.
struct RemoteReset {
    bool hard = false;
    bool ownerPrepared = false;
    bool responseSent = false;
    uint32_t requestId = 0;
    uint32_t requestedAtMs = 0;
    uint32_t lastCheckMs = 0;
    uint32_t lastWarningMs = 0;
    String rpcId;
} remoteReset;
String incomingResetRpcId; // valid only during synchronous processReq()
// OCPP task only. Operation keeps its own result until the reply has been sent.
struct MaxPowerChange {
    uint32_t requestId = 0;
    bool completed = false;
    bool saved = false;
};
std::shared_ptr<MaxPowerChange> pendingMaxPowerChange;
uint32_t s_grantRequestId = 0; // copied command identity, protected by s_stateMux
uint32_t s_latestRfidRequestId = 0; // protected by s_stateMux; resets rejection per presentation
uint32_t authorizationRfidRequestId = 0; // current transaction's RFID request (OCPP task only)
struct LocalRfidSettings {
    uint32_t magic = 0x4C524601;
    bool enabled = true;
    uint8_t report = 0; // 0 none, 1 actual, 2 custom
    char customId[21]{};
};
LocalRfidSettings s_localRfid;

constexpr size_t LOG_CAPACITY = 32;
struct LogEntry { uint32_t atMs; char text[112]; };
LogEntry s_log[LOG_CAPACITY]{};
size_t s_logNext = 0;
size_t s_logCount = 0;
uint32_t s_logRevision = 0;
// V2 is one atomic NVS blob: unchanged POD header followed by a NUL-terminated
// public CA PEM. V1 (WS-only) settings remain readable. No SPIFFS dependency.
constexpr uint32_t CONFIG_MAGIC_V1 = 0x4F435001;
constexpr uint32_t CONFIG_MAGIC = 0x4F435002;
constexpr size_t CA_PEM_MAX = 4096;
constexpr const char* CA_STORAGE_ID = "nvs:ocpp/config";
char s_caPem[CA_PEM_MAX + 1]{};
struct StoredConfig { uint32_t magic; ocpp_config_t config; };

bool validate_ca(const char* pem, String& error) {
    if (!pem || !*pem || strlen(pem) > CA_PEM_MAX) {
        error = "Select a CA PEM file (maximum 4096 bytes).";
        return false;
    }
    // Accept certificate PEM only, never a private key, DER, or partial bundle.
    const char* cursor = pem;
    unsigned count = 0;
    while (*cursor) {
        while (*cursor && strchr(" \r\n\t", *cursor)) ++cursor;
        if (!*cursor) break;
        const char* begin = "-----BEGIN CERTIFICATE-----";
        const char* end = "-----END CERTIFICATE-----";
        if (strncmp(cursor, begin, strlen(begin))) {
            error = "CA file must contain only PEM certificates, not private keys.";
            return false;
        }
        const char* finish = strstr(cursor + strlen(begin), end);
        if (!finish || ++count > 3) {
            error = "Invalid CA PEM bundle (maximum three certificates).";
            return false;
        }
        cursor = finish + strlen(end);
    }
    mbedtls_x509_crt chain;
    mbedtls_x509_crt_init(&chain);
    const int result = mbedtls_x509_crt_parse(&chain,
        reinterpret_cast<const unsigned char*>(pem), strlen(pem) + 1);
    bool valid = count && result == 0;
    for (auto cert = &chain; valid && cert; cert = cert->next) {
        valid = cert->raw.len && cert->ca_istrue;
    }
    mbedtls_x509_crt_free(&chain);
    if (!valid) error = "Invalid CA certificate. Import a CA, not the server/leaf certificate.";
    return valid;
}

void log_event(const char* format, ...) {
    LogEntry entry{};
    entry.atMs = millis();
    va_list args;
    va_start(args, format);
    vsnprintf(entry.text, sizeof(entry.text), format, args);
    va_end(args);
    portENTER_CRITICAL(&s_stateMux);
    s_log[s_logNext] = entry;
    s_logNext = (s_logNext + 1) % LOG_CAPACITY;
    if (s_logCount < LOG_CAPACITY) ++s_logCount;
    ++s_logRevision;
    portEXIT_CRITICAL(&s_stateMux);
    ESP_LOGI(TAG, "%s", entry.text);
}


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

ocpp_runtime_status_t s_status{};

// OCPP-Statusvariablen; die Zuweisungen stehen unten in der Task-Schleife.
bool Available = false;
bool Preparing = false;
bool Charging = false;
bool SuspendedEVSE = false;
bool SuspendedEV = false;
bool Finishing = false;
bool Reserved = false;
bool Unavailable = false;
bool Faulted = false;

const char* ErrorCode = nullptr;
bool MeterValid = false;
uint32_t meterCapturedAtMs = 0;
int64_t EnergyImportWh = 0;
float PowerImportW = 0.0f;
// Nur Anzeigehistorie, keine Ladefreigabe und keine OCPP-Transaktion.
bool chargingSeenSinceUnplug = false;

// Nur Status + Fehlercode entprellen, keine Messwerte oder Start/Stop-Ereignisse.
constexpr uint32_t OCPP_STATUS_DEBOUNCE_MS = 250;
ChargePointStatus pendingStatus = ChargePointStatus_UNDEFINED;
char pendingErrorCode[32]{};
uint32_t pendingStatusSinceMs = 0;
ChargePointStatus confirmedStatus = ChargePointStatus_UNDEFINED;
char confirmedErrorCode[32]{};
// UNDEFINED = noch kein bestaetigter Status; kein gueltiger OCPP-Sendewert.
uint32_t statusRevision = 1;
uint32_t acknowledgedStatusRevision[2]{};
bool statusRequestPending = false;
uint32_t lastStatusAttemptMs = 0;
constexpr uint32_t OCPP_METER_MAX_AGE_MS = 6000; // normal polling: 2 s
bool meterRequestPending = false;
bool meterTriggerPending = false;
bool meterSentOnce = false;
uint32_t lastMeterAttemptMs = 0;

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
    if (!id || !*id) return false;
    // Allow EVSE-style '*' separators while excluding URL path delimiters.
    for (const char* p = id; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || strchr("-._~*", *p))) return false;
    }
    return true;
}

bool parse_test_endpoint(const char* url, String& host, uint16_t& port, String& path) {
    const bool tls = starts_with(url, "wss://");
    if (!is_valid_endpoint(url, tls ? "wss://" : "ws://") || strpbrk(url, "?\\")) return false;
    String endpoint(url + (tls ? 6 : 5));
    const int slash = endpoint.indexOf('/');
    String authority = slash < 0 ? endpoint : endpoint.substring(0, slash);
    path = slash < 0 ? "/" : endpoint.substring(slash);
    const int colon = authority.indexOf(':');
    host = colon < 0 ? authority : authority.substring(0, colon);
    if (host.isEmpty()) return false;
    for (size_t i = 0; i < host.length(); ++i) {
        const char c = host[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '.')) return false;
    }
    uint32_t number = tls ? 443 : 80;
    if (colon >= 0) {
        String value = authority.substring(colon + 1);
        if (value.isEmpty() || value.length() > 5) return false;
        number = 0;
        for (size_t i = 0; i < value.length(); ++i) {
            if (value[i] < '0' || value[i] > '9') return false;
            number = number * 10 + value[i] - '0';
        }
    }
    if (!number || number > 65535) return false;
    port = uint16_t(number);
    return true;
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
    const bool changed = strcmp(s_status.lastError, message ? message : "") != 0;
    set_last_error_locked(message);
    portEXIT_CRITICAL(&s_stateMux);
    if (changed && message && *message) log_event("Error: %s", message);
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
        *error = "Charge Point ID must use letters, digits or - . _ ~ *";
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
            if (!ocpp_auth::valid_password(config.authorizationKey)) {
                *error = "Password: 16-20 printable ASCII characters, or hex: followed by 32-40 hex digits";
                return false;
            }
            if (strcmp(config.caCertificatePath, CA_STORAGE_ID)) {
                *error = "Security Profile 2 requires an imported CA certificate";
                return false;
            }
            return true;

        case ocpp_security_profile_t::UnsecuredDevelopment:
            if (!config.allowUnsecuredDevelopment ||
                !is_valid_endpoint(config.backendUrl, "ws://")) {
                *error = "Unsecured OCPP requires an explicit ws:// configuration";
                return false;
            }
            return true;

        default:
            *error = "Unsupported OCPP security profile";
            return false;
    }
}

bool stack_is_authoritative() {
    bool initialized = false;
    portENTER_CRITICAL(&s_stateMux);
    initialized = s_config.enabled && !s_configSaving &&
        s_appliedRevision == s_configRevision && s_status.bootAccepted &&
        s_status.websocketConnected;
    portEXIT_CRITICAL(&s_stateMux);
    return initialized;
}

bool save_backend_settings(const BackendSettings& settings) {
    Preferences store;
    if (!store.begin("ocppControl", false)) return false;
    const bool ok = store.putBytes("settings", &settings, sizeof(settings)) == sizeof(settings);
    store.end();
    if (!ok) set_last_error("Backend settings could not be saved; unchanged");
    return ok;
}

void publish_backend_settings() {
    portENTER_CRITICAL(&s_stateMux);
    s_backendOperative = backendSettings.operative != 0;
    s_status.backendOperative = s_backendOperative;
    s_status.heartbeatIntervalSeconds = backendSettings.heartbeatSeconds;
    s_status.meterIntervalSeconds = backendSettings.meterSeconds;
    portEXIT_CRITICAL(&s_stateMux);
}

void load_backend_settings(const ocpp_config_t& config) {
    BackendSettings saved;
    Preferences store;
    bool loaded = store.begin("ocppControl", true);
    if (loaded) {
        const size_t size = store.getBytesLength("settings");
        if (size == sizeof(saved)) {
            loaded = store.getBytes("settings", &saved, sizeof(saved)) == sizeof(saved);
        } else if (size == sizeof(BackendSettingsV1)) {
            loaded = store.getBytes("settings", static_cast<BackendSettingsV1*>(&saved), size) == size;
            // Older blobs have no retry settings. Keep the tested MicroOCPP defaults.
            saved.transactionMessageAttempts = 3;
            saved.transactionMessageRetrySeconds = 60;
        } else loaded = false;
    }
    store.end();
    const bool valid = loaded && saved.magic == backendSettings.magic && saved.operative <= 1 &&
        saved.heartbeatSeconds >= 1 && saved.heartbeatSeconds <= 86400 && saved.meterSeconds <= 86400 &&
        saved.transactionMessageAttempts >= 1 && saved.transactionMessageAttempts <= 100 &&
        saved.transactionMessageRetrySeconds <= 86400 &&
        has_terminator(saved.backendUrl) && has_terminator(saved.chargePointId) &&
        !strcmp(saved.backendUrl, config.backendUrl) && !strcmp(saved.chargePointId, config.chargePointId);
    backendSettings = valid ? saved : BackendSettings{};
    copy_text(backendSettings.backendUrl, sizeof(backendSettings.backendUrl), config.backendUrl);
    copy_text(backendSettings.chargePointId, sizeof(backendSettings.chargePointId), config.chargePointId);
    publish_backend_settings();
}

void remember_boot_heartbeat(int seconds) {
    // BootNotification.conf is authoritative for the new heartbeat, not an old saved value.
    if (seconds < 1 || seconds > 86400 || uint32_t(seconds) == backendSettings.heartbeatSeconds) return;
    BackendSettings next = backendSettings;
    next.heartbeatSeconds = uint32_t(seconds);
    if (!save_backend_settings(next)) log_event("Boot heartbeat applied; persistence failed");
    backendSettings = next;
    publish_backend_settings();
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

bool network_has_address() {
    ethernet_state_t eth{};
    get_ethernet_state(&eth);
    const bool ethernetOnline =
        eth.connection_state == ETHERNET_STATUS_CONNECTED &&
        (eth.ip_addr[0] || eth.ip_addr[1] || eth.ip_addr[2] || eth.ip_addr[3]);

    // Our ESP-IDF WLAN manager owns the status; Arduino WiFi.status() is not updated.
    wifi_sta_state_t wifi{};
    get_wifi_sta_state(&wifi);
    const bool wifiOnline =
        wifi.connected && (wifi.ip[0] || wifi.ip[1] || wifi.ip[2] || wifi.ip[3]);
    return ethernetOnline || wifiOnline;
}

// The Ethernet manager initializes ESP-IDF networking without Arduino WiFi's
// event group. Arduino 2.0.14 WiFi.hostByName() nevertheless uses that group in
// its DNS callback: a hostname connection can assert after an early return,
// also leaving its callback with a pointer to an expired stack IPAddress.
// Use lwIP's thread-safe netdb API from this task, for both WS and WSS. DNS
// waits only in the OCPP task and uses lwIP's own retry/timeout handling.
bool resolve_ocpp_host(const char* host, IPAddress& address) {
    if (!host || !*host) return false;
    if (address.fromString(host)) return uint32_t(address) != 0;
    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* result = nullptr;
    const int error = lwip_getaddrinfo(host, nullptr, &hints, &result);
    bool found = false;
    if (!error) {
        for (auto entry = result; entry; entry = entry->ai_next) {
            if (entry->ai_family != AF_INET || !entry->ai_addr ||
                entry->ai_addrlen < sizeof(sockaddr_in)) continue;
            const auto ipv4 = reinterpret_cast<const sockaddr_in*>(entry->ai_addr);
            address = IPAddress(ipv4->sin_addr.s_addr);
            if (uint32_t(address) != 0) { found = true; break; }
        }
    }
    if (result) lwip_freeaddrinfo(result);
    return found;
}

// MicroOCPP retains ownership of RPC IDs, retries, parsing and confirmations.
class OcppTlsClient : public WiFiClientSecure {
public:
    int connectResolved(IPAddress address, uint16_t port, const char* hostname) {
        _timeout = 5000; // TCP connect timeout in milliseconds
        // The IP-only overload loses the hostname. Keep the original name
        // for SNI AND mbedTLS certificate-name verification; never setInsecure.
        return WiFiClientSecure::connect(address, port, hostname, _CA_cert, nullptr, nullptr);
    }
    int setTimeout(uint32_t /*libraryTimeout*/) override {
        // WebSockets passes milliseconds, this Arduino secure-client API
        // interprets seconds. Keep both the stream and socket timeout at 5 s.
        Stream::setTimeout(5000);
        return WiFiClientSecure::setTimeout(5);
    }
    bool usesTls12() const {
        return strcmp(mbedtls_ssl_get_version(&sslclient->ssl_ctx), "TLSv1.2") == 0;
    }
};

class OcppSocket : public WebSocketsClient {
    unsigned long reportedFailure = 0;
    bool httpFailure = false;
protected:
    void clientDisconnect(WSclient_t* client) override {
        if (client->cCode >= 400) {
            char error[96];
            snprintf(error, sizeof(error), "WebSocket HTTP %u: check backend profile and credentials",
                     unsigned(client->cCode));
            set_last_error(error);
            httpFailure = true;
        }
        WebSocketsClient::clientDisconnect(client);
    }
public:
    // WebSockets 2.4.1 allocates TLS clients internally with a 120 s handshake
    // timeout and exposes no TLS-version policy. Keep its framing/heartbeat
    // implementation, but own DNS and the connect step. Reject older negotiated
    // versions BEFORE connectedCb() can send HTTP headers / Basic credentials.
    void poll() {
        if (!_port) return;
        if (!clientIsConnected(&_client)) {
            if (millis() - _lastConnectionFail < _reconnectInterval) return;
            if (_client.isSSL && (!_CA_cert || !*_CA_cert || !time_service_is_valid())) return;
            IPAddress address;
            if (!resolve_ocpp_host(_host.c_str(), address)) {
                _lastConnectionFail = millis();
                reportedFailure = _lastConnectionFail;
                set_last_error("DNS resolution failed; check DNS server/network (retrying)");
                return;
            }
            // DNS can take several seconds. Do not open a connection if the
            // user disabled OCPP or changed settings while resolution ran.
            portENTER_CRITICAL(&s_stateMux);
            const bool changed = s_configRevision != s_appliedRevision;
            portEXIT_CRITICAL(&s_stateMux);
            if (changed || !network_has_address()) return;
            if (!_client.isSSL) {
                auto client = new (std::nothrow) WiFiClient();
                if (!client) {
                    _lastConnectionFail = millis();
                    reportedFailure = _lastConnectionFail;
                    set_last_error("Insufficient memory for WS client");
                    return;
                }
                _client.tcp = client;
                if (client->connect(address, _port, 5000)) {
                    _lastConnectionFail = 0;
                    httpFailure = false;
                    connectedCb();
                } else {
                    clientDisconnect(&_client);
                    reportedFailure = _lastConnectionFail;
                    set_last_error("WS connection failed; check network and backend (retrying)");
                }
                return;
            }
            if (_client.ssl) clientDisconnect(&_client);
            auto client = new (std::nothrow) OcppTlsClient();
            if (!client) {
                set_last_error("Insufficient memory for TLS client");
                _lastConnectionFail = millis();
                reportedFailure = _lastConnectionFail;
                return;
            }
            _client.ssl = client;
            _client.tcp = client;
            client->setCACert(_CA_cert);
            client->setHandshakeTimeout(8); // seconds; enable/off applies after this bounded attempt
            if (client->connectResolved(address, _port, _host.c_str())) {
                if (!client->usesTls12()) {
                    clientDisconnect(&_client);
                    set_last_error("Backend negotiated unsupported TLS version; TLS 1.2 required");
                    reportedFailure = _lastConnectionFail;
                    return;
                }
                _lastConnectionFail = 0;
                httpFailure = false;
                connectedCb();
            } else {
                char detail[96];
                const int errorCode = client->lastError(detail, sizeof(detail));
                clientDisconnect(&_client);
                char error[96];
                snprintf(error, sizeof(error), "TLS connection failed (%d): check network, CA, hostname and time", errorCode);
                set_last_error(error);
                reportedFailure = _lastConnectionFail;
            }
        } else {
            handleClientData();
            if (_client.status == WSC_CONNECTED) {
                handleHBPing();
                handleHBTimeout(&_client);
            }
        }
    }

    void reportFailure() {
        if (!_lastConnectionFail || _lastConnectionFail == reportedFailure) return;
        reportedFailure = _lastConnectionFail;
        if (!httpFailure) {
            int tlsError = 0;
            if (_client.isSSL && _client.ssl) {
                char detail[96];
                tlsError = _client.ssl->lastError(detail, sizeof(detail));
            }
            if (tlsError) {
                char error[96];
                snprintf(error, sizeof(error), "TLS handshake failed (%d): check CA, hostname and device time", tlsError);
                set_last_error(error);
            } else {
                set_last_error("Connection interrupted/failed; retrying (network, TLS or WebSocket)");
            }
        }
        httpFailure = false;
    }
};

// MicroOCPP also closes StopSync on CALLERROR. Only a CALLRESULT is confirmation.
bool stopResponseConfirmed = false;
bool startResponseConfirmed = false; // A library Sync flag alone may also mean retries exhausted.
// Eigener Nachsendeauftrag, getrennt von der aktuell autorisierten Transaktion.
int32_t recoveryStopTransactionId = -1;
constexpr uint32_t RECOVERY_STOP_RETRY_MS = 60000;
uint32_t lastRecoveryStopAttemptMs = 0;
uint32_t recoveryStartLocalId = 0;
uint32_t lastRecoveryStartAttemptMs = 0;
bool recoveryStartFrame = false; // createReq -> sendTXT, same OCPP task/call stack
bool persist_start_before_send(JsonObjectConst payload);

class TestConnection : public MicroOcpp::EspWiFi::WSClient {
    String pendingId;
    String pendingAction;
    bool pendingRecoveryStop = false;
    bool pendingRecoveryStart = false;
    uint32_t pendingRfidRequestId = 0;
    char pendingRfidTag[OCPP_ID_TAG_MAX_LEN + 1]{};
    String statusFrameId;
    String statusFrame;
    uint32_t sentStatusRevision = 0;
    unsigned sentStatusConnector = 0;
    ChargePointStatus sentWireStatus = ChargePointStatus_UNDEFINED;
    uint32_t lastStatusSendMs = 0;
    OcppSocket* transport;
public:
    bool bootAccepted = false;
    bool bootRejected = false;
    bool firstHeartbeatQueued = false;

    explicit TestConnection(OcppSocket* socket)
        : MicroOcpp::EspWiFi::WSClient(socket), transport(socket) {}

    void loop() override {
        transport->poll();
        // Automatic library status requests have no timeout. A lost response
        // must not block its RPC queue forever, especially after reconnect.
        // Retry the identical RPC, never fabricate a backend confirmation.
        if (bootAccepted && time_service_is_valid() &&
            pendingAction == "StatusNotification" && pendingId == statusFrameId &&
            !statusFrame.isEmpty() && transport->isConnected() &&
            uint32_t(millis() - lastStatusSendMs) >= 10000) {
            lastStatusSendMs = millis();
            if (MicroOcpp::EspWiFi::WSClient::sendTXT(statusFrame.c_str(), statusFrame.length()))
                log_event("Retry StatusNotification c%u: %s", sentStatusConnector,
                          MicroOcpp::cstrFromOcppEveState(sentWireStatus));
        }
    }

    bool sendTXT(const char* message, size_t length) override {
        const bool recoveredStart = recoveryStartFrame;
        recoveryStartFrame = false;
        JsonDocument doc;
        const bool call = length <= 8192 && !deserializeJson(doc, message, length) &&
                          doc[0].as<int>() == 2;
        if (call && !strcmp(doc[2] | "", "StartTransaction") && !recoveredStart &&
            !persist_start_before_send(doc[3].as<JsonObjectConst>())) return false;
        if (call && !strcmp(doc[2] | "", "StatusNotification")) {
            // MicroOCPP's connector derives status from its own transactions.
            // Physical status remains CP-owned. Replace ONLY the status
            // payload here, including automatic / triggered notifications.
            // RPC IDs, acknowledgements and retries remain library-owned.
            if (!bootAccepted || confirmedStatus == ChargePointStatus_UNDEFINED ||
                !time_service_is_valid()) return false;
            const char* id = doc[1] | "";
            if (statusFrameId != id || statusFrame.isEmpty()) {
                const unsigned connector = doc[3]["connectorId"] | 0U;
                if (connector > OCPP_CONNECTOR_ID) return false;
                const ChargePointStatus wireStatus = connector == 0 &&
                    confirmedStatus != ChargePointStatus_Faulted &&
                    confirmedStatus != ChargePointStatus_Unavailable ?
                    ChargePointStatus_Available : confirmedStatus;
                JsonObject payload = doc[3].as<JsonObject>();
                payload.clear();
                payload["connectorId"] = connector;
                payload["status"] = MicroOcpp::cstrFromOcppEveState(wireStatus);
                payload["errorCode"] = confirmedErrorCode;
                char timestamp[JSONDATE_LENGTH + 1]{};
                getOcppContext()->getModel().getClock().now().toJsonString(timestamp, sizeof(timestamp));
                payload["timestamp"] = timestamp;
                statusFrame = "";
                serializeJson(doc, statusFrame);
                statusFrameId = id;
                sentStatusRevision = statusRevision;
                sentStatusConnector = connector;
                sentWireStatus = wireStatus;
            }
            // A retry of the same RPC must keep the same payload/timestamp.
            message = statusFrame.c_str();
            length = statusFrame.length();
        }
        if (!MicroOcpp::EspWiFi::WSClient::sendTXT(message, length)) return false;
        // A response-created callback is too early: observe actual transport acceptance.
        if (remoteReset.requestId && doc[0].as<int>() == 3 &&
            remoteReset.rpcId == (doc[1] | "") &&
            !strcmp(doc[2]["status"] | "", "Accepted")) {
            remoteReset.responseSent = true;
        }
        // MicroOCPP serializes outbound requests; keep only its current CALL.
        if (call) {
            pendingId = doc[1].as<const char*>();
            pendingAction = doc[2].as<const char*>();
            pendingRecoveryStart = pendingAction == "StartTransaction" && recoveredStart;
            pendingRecoveryStop = pendingAction == "StopTransaction" && recoveryStopTransactionId >= 0 &&
                doc[3]["transactionId"].as<int32_t>() == recoveryStopTransactionId;
            if (pendingAction == "Authorize") {
                pendingRfidRequestId = authorizationRfidRequestId;
                copy_text(pendingRfidTag, sizeof(pendingRfidTag), doc[3]["idTag"] | "");
            }
            if (pendingAction == "StatusNotification") {
                lastStatusSendMs = millis();
                log_event("TX StatusNotification c%u: %s", sentStatusConnector,
                          MicroOcpp::cstrFromOcppEveState(sentWireStatus));
            } else {
                log_event("TX %.40s", pendingAction.c_str());
            }
        }
        return true;
    }

    void setReceiveTXTcallback(MicroOcpp::ReceiveTXTcallback& callback) override {
        MicroOcpp::ReceiveTXTcallback observed =
            [this, callback](const char* message, size_t length) {
                // Large list/profile transfers are not supported.
                if (length > 8192) {
                    set_last_error("OCPP frame exceeds 8192 bytes");
                    return false;
                }
                JsonDocument doc;
                const bool valid = !deserializeJson(doc, message, length);
                incomingResetRpcId = valid && doc[0].as<int>() == 2 &&
                    !strcmp(doc[2] | "", "Reset") ? (doc[1] | "") : "";
                const bool processed = callback(message, length);
                incomingResetRpcId = "";
                if (!processed || !valid) return processed;
                const char* id = doc[1] | "";
                const int type = doc[0] | 0;
                if (type == 2) log_event("RX %.40s", doc[2] | "Unknown");
                if (pendingId.isEmpty() || pendingId != id || (type != 3 && type != 4))
                    return processed;
                if (type == 4) {
                    set_last_error("Backend returned an OCPP CALLERROR; see serial log");
                    log_event("RX CALLERROR for %.40s", pendingAction.c_str());
                } else if (pendingAction == "StartTransaction" && !pendingRecoveryStart) {
                    int32_t transactionId = -1;
                    startResponseConfirmed = ocpp_policy::start_reply_id(doc[2].as<JsonObjectConst>(), transactionId);
                } else if (pendingAction == "StopTransaction" && !pendingRecoveryStop) {
                    stopResponseConfirmed = doc[2].is<JsonObject>();
                } else if (pendingAction == "Authorize") {
                    const char* result = doc[2]["idTagInfo"]["status"] | "";
                    const bool rejected = !strcmp(result, "Blocked") || !strcmp(result, "Expired") ||
                        !strcmp(result, "Invalid") || !strcmp(result, "ConcurrentTx");
                    // Only an explicit, schema-defined rejection for this presentation.
                    // Malformed replies / CALLERROR / timeout / an older card are not denials.
                    portENTER_CRITICAL(&s_stateMux);
                    const bool current = pendingRfidRequestId &&
                        pendingRfidRequestId == s_latestRfidRequestId;
                    if (current) {
                        s_status.rfidRejected = rejected;
                        copy_text(s_status.rfidRejectedTag, sizeof(s_status.rfidRejectedTag),
                            rejected ? pendingRfidTag : "");
                    }
                    portEXIT_CRITICAL(&s_stateMux);
                    if (current) log_event("RFID Authorize result: %.24s", *result ? result : "invalid response");
                } else if (pendingAction == "BootNotification") {
                    JsonObject payload = doc[2].as<JsonObject>();
                    MicroOcpp::Timestamp timestamp;
                    const char* utc = payload["currentTime"] | "";
                    const char* status = payload["status"] | "";
                    if (payload["interval"].is<int>() &&
                        payload["interval"].as<int>() >= 0 && timestamp.setTime(utc)) {
                        bootAccepted = strcmp(status, "Accepted") == 0;
                        bootRejected = strcmp(status, "Rejected") == 0;
                        if (bootAccepted) {
                            set_last_error("");
                            remember_boot_heartbeat(payload["interval"].as<int>());
                        }
                        else set_last_error(bootRejected ? "BootNotification rejected" :
                                             "BootNotification pending or invalid");
                        log_event("BootNotification: %s; interval=%d s",
                                 bootAccepted ? "Accepted" : (bootRejected ? "Rejected" : "Pending/invalid"),
                                 payload["interval"].as<int>());
                    }
                } else if (pendingAction == "StatusNotification" && statusFrameId == id) {
                    acknowledgedStatusRevision[sentStatusConnector] = sentStatusRevision;
                    log_event("StatusNotification c%u confirmed", sentStatusConnector);
                } else if (pendingAction == "Heartbeat") {
                    MicroOcpp::Timestamp timestamp;
                    if (timestamp.setTime(doc[2]["currentTime"] | "")) {
                        portENTER_CRITICAL(&s_stateMux);
                        ++s_status.heartbeatConfirmations;
                        s_status.lastHeartbeatTimeMs = millis();
                        portEXIT_CRITICAL(&s_stateMux);
                        log_event("Heartbeat confirmed");
                    }
                }
                pendingId = "";
                pendingAction = "";
                if (statusFrameId == id) {
                    statusFrameId = "";
                    statusFrame = "";
                }
                return processed;
            };
        MicroOcpp::EspWiFi::WSClient::setReceiveTXTcallback(observed);
    }
};

// Explicitly reject commands whose charger-side implementation is not wired.
// The library still serializes the correct OCPP response/CALLERROR envelope.
class TestBlockedOperation : public MicroOcpp::Operation {
    const char* action;
    const char* responseStatus;
public:
    TestBlockedOperation(const char* action, const char* responseStatus)
        : action(action), responseStatus(responseStatus) {}
    const char* getOperationType() override { return action; }
    void processReq(JsonObject) override {
        log_event("%s blocked: operation not integrated", action);
    }
    std::unique_ptr<MicroOcpp::JsonDoc> createConf() override {
        auto doc = MicroOcpp::makeJsonDoc("InnoCharge.test", 128);
        if (responseStatus) (*doc)["status"] = responseStatus;
        return doc;
    }
    const char* getErrorCode() override {
        return responseStatus ? nullptr : "NotSupported";
    }
    const char* getErrorDescription() override { return "Operation not integrated"; }
};

class TestTriggerMessage : public MicroOcpp::Ocpp16::TriggerMessage {
    bool supported = false;
    const char* ownStatus = nullptr;
public:
    explicit TestTriggerMessage(MicroOcpp::Context& context)
        : MicroOcpp::Ocpp16::TriggerMessage(context) {}
    void processReq(JsonObject payload) override {
        const char* action = payload["requestedMessage"] | "";
        if (!strcmp(action, "MeterValues")) {
            const int connector = payload["connectorId"] | 0;
            const bool accepted = (connector == 0 || connector == OCPP_CONNECTOR_ID) &&
                MeterValid && time_service_is_valid() && !meterRequestPending && !meterTriggerPending &&
                uint32_t(millis() - meterCapturedAtMs) <= OCPP_METER_MAX_AGE_MS;
            ownStatus = accepted ? "Accepted" : "Rejected";
            if (accepted) meterTriggerPending = true;
            return;
        }
        if (!strcmp(action, "StatusNotification") && confirmedStatus == ChargePointStatus_UNDEFINED) {
            ownStatus = "Rejected";
            return;
        }
        supported = !strcmp(action, "BootNotification") ||
                    !strcmp(action, "Heartbeat") ||
                    !strcmp(action, "StatusNotification");
        if (supported) MicroOcpp::Ocpp16::TriggerMessage::processReq(payload);
    }
    std::unique_ptr<MicroOcpp::JsonDoc> createConf() override {
        if (supported && !ownStatus) return MicroOcpp::Ocpp16::TriggerMessage::createConf();
        auto doc = MicroOcpp::makeJsonDoc("InnoCharge.test", 128);
        (*doc)["status"] = ownStatus ? ownStatus : "NotImplemented";
        return doc;
    }
};

// Authorization uses the existing MB session. MicroOCPP owns only protocol
// state / transactionId. Neither it nor this adapter writes CP or relay outputs.
std::shared_ptr<MicroOcpp::Transaction> authorizationTx;
uint32_t authorizationStartedMs = 0;
uint32_t ownerSessionStamp = 0;
uint32_t grantSentMs = 0;
uint32_t stoppedAtMs = 0;
uint32_t lastStopCommandMs = 0;
uint32_t nextAuthorizationRequestId = 0;
bool remoteAuthorization = false;
bool authorizationInitiallyPlugged = false;
bool startConfirmationLogged = false;
bool stopMeterErrorLogged = false;
session_ocpp_record_t transactionRecord;
bool reportingExistingSession = false;
bool startJournalReady = false, stopJournalReady = false;
uint32_t lastReportCheckMs = 0;
uint32_t lastStartJournalAttemptMs = 0, lastStopJournalAttemptMs = 0;

MicroOcpp::Timestamp session_timestamp(time_t epoch) {
    struct tm utc{};
    gmtime_r(&epoch, &utc);
    return MicroOcpp::Timestamp(utc.tm_year + 1900, utc.tm_mon, utc.tm_mday - 1,
        utc.tm_hour, utc.tm_min, utc.tm_sec);
}

bool original_start_matches(JsonObjectConst payload, const session_ocpp_record_t& record) {
    if (!record.meterStartValid || record.startTime < 1577836800) return false;
    char timestamp[JSONDATE_LENGTH + 1]{};
    session_timestamp(record.startTime).toJsonString(timestamp, sizeof(timestamp));
    return ocpp_policy::start_matches(payload, record.ocpp.idTag, record.meterStartWh, timestamp);
}

bool persist_start_before_send(JsonObjectConst payload) {
    session_ocpp_record_t record;
    if (!transactionRecord.localId ||
        !charge_session_log_get_ocpp(transactionRecord.localId, 0, record) ||
        !original_start_matches(payload, record)) {
        set_last_error("StartTransaction blocked: original session boundary mismatch");
        return false;
    }
    String original;
    serializeJson(payload, original);
    const bool saved = charge_session_log_capture_start(record.localId, original) && charge_session_log_flush();
    if (!saved) set_last_error("StartTransaction waiting for original payload storage");
    return saved;
}

void finish_recovered_start(uint32_t localId, JsonObjectConst payload) {
    int32_t transactionId = -1;
    const bool updated = ocpp_policy::start_reply_id(payload, transactionId) &&
        charge_session_log_confirm_recovered_start(localId, transactionId);
    recoveryStartLocalId = 0;
    lastRecoveryStartAttemptMs = millis();
    if (!updated) {
        log_event("Recovered StartTransaction not confirmed: local=%lu; retry later", (unsigned long)localId);
        return;
    }
    // No grant / no new local session. Existing stop-recovery owns the next step.
    if (!charge_session_log_flush()) set_last_error("Recovered transactionId awaits durable storage");
    lastRecoveryStopAttemptMs = millis() - RECOVERY_STOP_RETRY_MS;
    log_event("Recovered StartTransaction: local=%lu, id=%ld; stop pending",
        (unsigned long)localId, (long)transactionId);
}

void service_recovered_start(MicroOcpp::Context& context, const ocpp_config_t& config) {
    if (s_resetPending || recoveryStartLocalId || recoveryStopTransactionId >= 0 ||
        !stack_is_authoritative() || uint32_t(millis() - lastRecoveryStartAttemptMs) < RECOVERY_STOP_RETRY_MS) return;
    session_ocpp_record_t record;
    String original;
    if (!charge_session_log_next_recovered_start(config.backendUrl, config.chargePointId, record, original)) return;
    lastRecoveryStartAttemptMs = millis();
    JsonDocument check;
    if (deserializeJson(check, original) || !original_start_matches(check.as<JsonObjectConst>(), record)) {
        set_last_error("Recovered StartTransaction payload invalid; manual review required");
        return;
    }
    if (!charge_session_log_flush()) return;
    const uint32_t localId = record.localId;
    auto request = MicroOcpp::makeRequest(new MicroOcpp::Ocpp16::CustomOperation(
        "StartTransaction", [original] {
            auto doc = MicroOcpp::makeJsonDoc("InnoCharge.recoveredStart", 512);
            if (deserializeJson(*doc, original)) return std::unique_ptr<MicroOcpp::JsonDoc>{};
            recoveryStartFrame = true; // This exact journal payload bypasses CURRENT-session capture.
            return doc;
        }, [localId](JsonObject payload) {
            finish_recovered_start(localId, payload);
        }));
    request->setTimeout(30000);
    request->setOnAbortListener([localId] { finish_recovered_start(localId, JsonObjectConst{}); });
    recoveryStartLocalId = localId;
    log_event("Replaying original StartTransaction: local=%lu; no charging grant", (unsigned long)localId);
    context.initiateRequest(std::move(request));
}

void finish_recovered_stop(uint32_t localId, int32_t transactionId, bool confirmed) {
    const auto state = confirmed ? session_ocpp_state_t::Confirmed : session_ocpp_state_t::Pending;
    const bool updated = charge_session_log_set_recovered_stop(localId, transactionId, state);
    const bool saved = updated && charge_session_log_flush();
    recoveryStopTransactionId = -1;
    lastRecoveryStopAttemptMs = millis();
    if (confirmed && updated) {
        log_event("Recovered StopTransaction confirmed: local=%lu, id=%ld",
            (unsigned long)localId, (long)transactionId);
        if (!saved) set_last_error("Recovery confirmation received; session storage still pending");
    } else {
        log_event("Recovered StopTransaction not confirmed: id=%ld; retry later", (long)transactionId);
    }
}

void service_recovered_stop(MicroOcpp::Context& context, const ocpp_config_t& config) {
    if (recoveryStartLocalId || recoveryStopTransactionId >= 0 || !stack_is_authoritative() ||
        uint32_t(millis() - lastRecoveryStopAttemptMs) < RECOVERY_STOP_RETRY_MS) return;
    session_ocpp_record_t record;
    if (!charge_session_log_next_recovered_stop(config.backendUrl, config.chargePointId, record)) return;
    lastRecoveryStopAttemptMs = millis();

    // Nachsendeauftrag VOR dem Senden sichern. Ein weiterer Neustart darf ihn nicht verlieren.
    if (!charge_session_log_set_recovered_stop(record.localId, record.ocpp.transactionId, session_ocpp_state_t::Sent) ||
        !charge_session_log_flush()) {
        set_last_error("Recovery StopTransaction waiting for session storage");
        return;
    }
    const uint32_t localId = record.localId;
    const int32_t transactionId = record.ocpp.transactionId;
    auto request = MicroOcpp::makeRequest(new MicroOcpp::Ocpp16::CustomOperation(
        "StopTransaction", [record] {
            auto doc = MicroOcpp::makeJsonDoc("InnoCharge.recoveredStop", 256);
            (*doc)["transactionId"] = record.ocpp.transactionId;
            (*doc)["meterStop"] = record.meterStopWh;
            char timestamp[JSONDATE_LENGTH + 1]{};
            session_timestamp(record.stopTime).toJsonString(timestamp, sizeof(timestamp));
            (*doc)["timestamp"] = timestamp;
            const char* reason = record.stopReason;
            if (!*reason || !strcmp(reason, "Faulted") || !strcmp(reason, "TimeLimit") || !strcmp(reason, "Stopped"))
                reason = "Other";
            (*doc)["reason"] = reason;
            return doc;
        }, [localId, transactionId](JsonObject payload) {
            // Nur CALLRESULT mit Objekt bestaetigt den alten Stopp, niemals CALLERROR/Timeout.
            finish_recovered_stop(localId, transactionId, !payload.isNull());
        }));
    request->setTimeout(30000);
    request->setOnAbortListener([localId, transactionId] {
        finish_recovered_stop(localId, transactionId, false);
    });
    recoveryStopTransactionId = transactionId;
    log_event("Recovering StopTransaction: local=%lu, id=%ld; stored meter/time",
        (unsigned long)localId, (long)transactionId);
    // Kein beginTransaction/GrantSession: nur den Abschluss der ALTEN ID senden.
    context.initiateRequest(std::move(request));
}

bool owner_session_matches() {
    return ownerSessionStamp && chargeAuthSession.authorized &&
        chargeAuthSession.authorizationGrantedMillis == ownerSessionStamp;
}

bool enqueue_session_stop(uint32_t stamp, const char* reason) {
    ocpp_command_t command{};
    command.type = ocpp_command_type_t::StopSession;
    command.requestId = ++nextAuthorizationRequestId;
    if (!command.requestId) command.requestId = ++nextAuthorizationRequestId;
    command.connectorId = OCPP_CONNECTOR_ID;
    command.sessionStamp = stamp;
    copy_text(command.stopReason, sizeof(command.stopReason), reason);
    if (xQueueSend(s_commandQueue, &command, 0) != pdPASS) return false;
    lastStopCommandMs = millis();
    return true;
}

bool begin_backend_authorization(const char* idTag, bool remote) {
    if (!getOcppContext() || !stack_is_authoritative() || !rfidAuth.required ||
        chargeAuthSession.authorized || authorizationTx || getTransaction() ||
        !ocpp_backend_accepts_cards() || Faulted || Unavailable || !MeterValid || EnergyImportWh > INT32_MAX ||
        !time_service_is_valid()) return false;
    portENTER_CRITICAL(&s_stateMux);
    const bool allowed = !s_configSaving && !s_authorizationBusy && !s_resetPending &&
        s_appliedRevision == s_configRevision;
    if (allowed) s_authorizationBusy = true;
    portEXIT_CRITICAL(&s_stateMux);
    if (!allowed) return false;
    // RemoteStart itself is the backend authorization (AuthorizeRemoteTxRequests=false).
    // RFID requests must first receive Authorize.conf Accepted.
    authorizationTx = remote ? beginTransaction_authorized(idTag) : beginTransaction(idTag);
    if (!authorizationTx) {
        portENTER_CRITICAL(&s_stateMux);
        s_authorizationBusy = false;
        portEXIT_CRITICAL(&s_stateMux);
        return false;
    }
    authorizationStartedMs = millis();
    transactionRecord = {};
    reportingExistingSession = startJournalReady = stopJournalReady = false;
    stopResponseConfirmed = false;
    startResponseConfirmed = false;
    lastStartJournalAttemptMs = lastStopJournalAttemptMs = millis() - 1000;
    ownerSessionStamp = grantSentMs = stoppedAtMs = 0;
    remoteAuthorization = remote;
    authorizationInitiallyPlugged = vCurrentCpState.vehicleConnected;
    startConfirmationLogged = stopMeterErrorLogged = false;
    lastStopCommandMs = millis() - 1000;
    log_event(remote ? "RemoteStart accepted; requesting session permission" : "RFID: awaiting backend authorization");
    return true;
}

void process_authorization_event(const input_event_t& event) {
    if (event.type == input_event_type_t::IdTagPresented) {
        if (uint32_t(millis() - event.createdAtMs) > 30000 ||
            !begin_backend_authorization(event.idTag, false))
            log_event("RFID backend request rejected: busy, offline or meter unavailable");
        else authorizationRfidRequestId = event.rfidRequestId;
    } else if (event.type == input_event_type_t::CommandFeedback &&
               event.commandFeedback.type == ocpp_command_type_t::SetMaxChargingPower) {
        const auto& feedback = event.commandFeedback;
        log_event("%s", feedback.detail);
        if (pendingMaxPowerChange && pendingMaxPowerChange->requestId == feedback.requestId) {
            pendingMaxPowerChange->saved = feedback.result == ocpp_command_result_t::Accepted;
            pendingMaxPowerChange->completed = true;
            pendingMaxPowerChange.reset();
        }
    } else if (event.type == input_event_type_t::CommandFeedback &&
               event.commandFeedback.type == ocpp_command_type_t::Reset) {
        if (remoteReset.requestId && event.commandFeedback.requestId == remoteReset.requestId) {
            remoteReset.ownerPrepared = event.commandFeedback.result == ocpp_command_result_t::Accepted;
            if (!remoteReset.ownerPrepared) set_last_error("Reset preparation rejected by charging owner");
        }
    } else if (event.type == input_event_type_t::CommandFeedback &&
               event.commandFeedback.type == ocpp_command_type_t::GrantSession) {
        const auto& feedback = event.commandFeedback;
        if (authorizationTx && feedback.requestId == s_grantRequestId) {
            if (feedback.result == ocpp_command_result_t::Accepted) {
                ownerSessionStamp = feedback.sessionStamp;
                log_event("Existing session authorized by OCPP");
            } else {
                authorizationTx->setInactive();
                authorizationTx->commit();
                log_event("OCPP grant declined by session owner");
            }
        } else if (feedback.result == ocpp_command_result_t::Accepted && feedback.sessionStamp) {
            // Even a late owner acknowledgement must not leave an orphan grant.
            if (!enqueue_session_stop(feedback.sessionStamp, "Other"))
                enqueue_event(event, 0);
        }
    } else if (event.type == input_event_type_t::LocalStop && authorizationTx) {
        static const char* const reasons[] = {"Local", "EVDisconnected", "DeAuthorized",
            "EmergencyStop", "HardReset", "Other", "PowerLoss", "Reboot", "Remote",
            "SoftReset", "UnlockCommand"};
        const char* reason = reasons[static_cast<unsigned>(event.stopReason)];
        // Existing session cancellation below determines transaction completion.
        if (owner_session_matches()) {
            if (enqueue_session_stop(ownerSessionStamp, reason)) endTransaction_authorized(nullptr, reason);
        } else endTransaction_authorized(nullptr, reason);
    }
}

void service_backend_authorization() {
    if (!s_resetPending && ocpp_backend_accepts_cards() && !authorizationTx && stack_is_authoritative() && !getTransaction() &&
        uint32_t(millis() - lastReportCheckMs) >= 1000) {
        lastReportCheckMs = millis();
        ocpp_config_t config{};
        portENTER_CRITICAL(&s_stateMux); config = s_config; portEXIT_CRITICAL(&s_stateMux);
        session_ocpp_record_t pending;
        if (charge_session_log_next_ocpp(config.backendUrl, config.chargePointId, pending)) {
            if (!pending.meterStartValid || pending.startTime < 1577836800 ||
                (!pending.active && (!pending.meterStopValid || pending.stopTime < pending.startTime))) {
                pending.ocpp.start = session_ocpp_state_t::Failed;
                charge_session_log_set_ocpp(pending.localId, pending.ocpp);
                set_last_error("Local report needs valid session meter/time; see Sessions");
            } else {
                portENTER_CRITICAL(&s_stateMux);
                const bool allowed = !s_configSaving && !s_authorizationBusy && !s_resetPending &&
                    s_appliedRevision == s_configRevision;
                if (allowed) s_authorizationBusy = true;
                portEXIT_CRITICAL(&s_stateMux);
                if (allowed) {
                    authorizationTx = beginTransaction_authorized(pending.ocpp.idTag);
                    if (authorizationTx) {
                        transactionRecord = pending;
                        reportingExistingSession = true;
                        startJournalReady = stopJournalReady = false;
                        stopResponseConfirmed = false;
                        startResponseConfirmed = false;
                        lastStartJournalAttemptMs = lastStopJournalAttemptMs = millis() - 1000;
                        ownerSessionStamp = pending.authorizationStamp;
                        authorizationStartedMs = grantSentMs = millis();
                        stoppedAtMs = 0;
                        startConfirmationLogged = stopMeterErrorLogged = false;
                        lastStopCommandMs = millis() - 1000;
                        log_event("Reporting existing session %lu; local permission unchanged", (unsigned long)pending.localId);
                    } else {
                        portENTER_CRITICAL(&s_stateMux); s_authorizationBusy = false; portEXIT_CRITICAL(&s_stateMux);
                    }
                }
            }
        }
    }
    if (!authorizationTx) return;
    auto& tx = *authorizationTx;
    const uint32_t now = millis();
    if (transactionRecord.localId || ownerSessionStamp)
        charge_session_log_get_ocpp(transactionRecord.localId, ownerSessionStamp, transactionRecord);

    // Durable association before the first possible StartTransaction attempt.
    // Local charging is already allowed and never waits for this storage/backend work.
    if (transactionRecord.localId && !startJournalReady && transactionRecord.meterStartValid &&
        transactionRecord.startTime >= 1577836800) {
        tx.setMeterStart(transactionRecord.meterStartWh);
        tx.setStartTimestamp(session_timestamp(transactionRecord.startTime));
        tx.setStartBootNr(getOcppContext()->getModel().getBootNr());
        if (transactionRecord.ocpp.start == session_ocpp_state_t::Pending) {
            transactionRecord.ocpp.start = session_ocpp_state_t::Sent;
            charge_session_log_set_ocpp(transactionRecord.localId, transactionRecord.ocpp);
        }
        if (uint32_t(now - lastStartJournalAttemptMs) >= 1000) {
            lastStartJournalAttemptMs = now;
            startJournalReady = charge_session_log_flush();
        }
    }
    if (tx.getStartSync().isConfirmed() && tx.getTransactionId() < 0 && tx.isActive()) {
        tx.setIdTagDeauthorized();
        tx.setInactive();
        tx.setStopReason("DeAuthorized");
        tx.commit();
        set_last_error("StartTransaction response has no valid transactionId");
    }
    if (!reportingExistingSession && !ownerSessionStamp && tx.isActive() &&
        (!rfidAuth.required || !ocpp_backend_accepts_cards() || Faulted ||
         uint32_t(now - authorizationStartedMs) >= 30000 ||
         (grantSentMs && uint32_t(now - grantSentMs) >= 5000))) {
        tx.setInactive();
        tx.commit();
        log_event("OCPP authorization expired/cancelled");
    }
    if (ocpp_backend_accepts_cards() && !reportingExistingSession && !grantSentMs && tx.isActive() && tx.isAuthorized() && !tx.isIdTagDeauthorized()) {
        if (!MeterValid || EnergyImportWh > INT32_MAX) return;
        ocpp_command_t command{};
        command.type = ocpp_command_type_t::GrantSession;
        command.requestId = ++nextAuthorizationRequestId;
        if (!command.requestId) command.requestId = ++nextAuthorizationRequestId;
        command.connectorId = OCPP_CONNECTOR_ID;
        command.expiresAtMs = now + 5000;
        command.remoteStart = remoteAuthorization;
        command.vehicleConnectedAtRequest = authorizationInitiallyPlugged;
        copy_text(command.idTag, sizeof(command.idTag), tx.getIdTag());
        // Capture before granting power, not after StartTransaction.conf.
        tx.setMeterStart(int32_t(EnergyImportWh));
        tx.commit();
        portENTER_CRITICAL(&s_stateMux);
        s_grantRequestId = command.requestId;
        portEXIT_CRITICAL(&s_stateMux);
        if (xQueueSend(s_commandQueue, &command, 0) == pdPASS) grantSentMs = now;
    }
    if (owner_session_matches()) {
        if ((!tx.isActive() || tx.isIdTagDeauthorized()) &&
            uint32_t(now - lastStopCommandMs) >= 1000) {
            const char* reason = tx.isIdTagDeauthorized() ? "DeAuthorized" : tx.getStopReason();
            enqueue_session_stop(ownerSessionStamp, *reason ? reason : "Other");
        }
    } else if ((ownerSessionStamp || reportingExistingSession) &&
               (!reportingExistingSession || tx.getStartSync().isRequested())) {
        if (!stoppedAtMs) {
            stoppedAtMs = now;
            // Record the actual local end, even when delivery waits for reconnect.
            auto& model = getOcppContext()->getModel();
            tx.setStopTimestamp(model.getClock().now());
            tx.setStopBootNr(model.getBootNr());
            tx.commit();
        }
        if (tx.isActive()) {
            const char* reason = transactionRecord.stopReason;
            if (!strcmp(reason, "Faulted") || !strcmp(reason, "TimeLimit") || !strcmp(reason, "Stopped")) reason = "Other";
            endTransaction_authorized(nullptr, *reason ? reason :
                vCurrentCpState.state == StateA_NotConnected ? "EVDisconnected" : "Local");
            log_event("Existing session ended; completing OCPP transaction");
        }
        // Use THIS session's recorded boundary, never a reading from a later charge.
        if (transactionRecord.localId && !transactionRecord.active && transactionRecord.meterStopValid) {
            if (!tx.isMeterStopDefined()) {
                tx.setMeterStop(transactionRecord.meterStopWh);
                tx.setStopTimestamp(session_timestamp(transactionRecord.stopTime));
                tx.commit();
            }
            if (transactionRecord.ocpp.stop == session_ocpp_state_t::Pending) {
                transactionRecord.ocpp.stop = session_ocpp_state_t::Sent;
                charge_session_log_set_ocpp(transactionRecord.localId, transactionRecord.ocpp);
            }
            if (!stopJournalReady && uint32_t(now - lastStopJournalAttemptMs) >= 1000) {
                lastStopJournalAttemptMs = now;
                stopJournalReady = charge_session_log_flush();
            }
        } else if (uint32_t(now - stoppedAtMs) >= 10000 && !stopMeterErrorLogged) {
            set_last_error("StopTransaction needs recorded final meter; see Sessions");
            stopMeterErrorLogged = true;
        }
    }
    if (tx.getStartSync().isConfirmed() && !startConfirmationLogged) {
        const bool confirmed = startResponseConfirmed && tx.getTransactionId() >= 0;
        transactionRecord.ocpp.transactionId = confirmed ? tx.getTransactionId() : -1;
        transactionRecord.ocpp.start = confirmed ? session_ocpp_state_t::Confirmed : session_ocpp_state_t::Uncertain;
        charge_session_log_set_ocpp(transactionRecord.localId, transactionRecord.ocpp);
        charge_session_log_flush();
        if (confirmed) log_event("StartTransaction confirmed: id=%d, %s", tx.getTransactionId(),
            tx.isIdTagDeauthorized() ? "deauthorized" : "accepted");
        else set_last_error("StartTransaction has no valid wire confirmation; original start retained for reboot recovery");
        startConfirmationLogged = true;
    }
    if ((tx.isAborted() || tx.isCompleted() ||
         (tx.isSilent() && tx.getStopSync().isRequested())) && !owner_session_matches()) {
        if (transactionRecord.localId) {
            transactionRecord.ocpp.stop = tx.isCompleted() && !tx.isSilent() && stopResponseConfirmed ?
                session_ocpp_state_t::Confirmed : session_ocpp_state_t::Failed;
            // Reset must not turn a recoverable, unacknowledged stop into Failed.
            if (s_resetPending && transactionRecord.ocpp.stop != session_ocpp_state_t::Confirmed &&
                transactionRecord.ocpp.start == session_ocpp_state_t::Confirmed &&
                transactionRecord.ocpp.transactionId >= 0)
                transactionRecord.ocpp.stop = session_ocpp_state_t::Pending;
            charge_session_log_set_ocpp(transactionRecord.localId, transactionRecord.ocpp);
            charge_session_log_flush();
        }
        if (tx.isSilent() || (tx.isCompleted() && !stopResponseConfirmed))
            set_last_error("OCPP transaction delivery failed; not confirmed by backend");
        else log_event(tx.isCompleted() ? "StopTransaction confirmed" : "OCPP authorization ended without transaction");
        authorizationTx.reset();
        ownerSessionStamp = 0;
        transactionRecord = {};
        reportingExistingSession = startJournalReady = stopJournalReady = false;
        portENTER_CRITICAL(&s_stateMux);
        s_grantRequestId = 0;
        s_authorizationBusy = false;
        portEXIT_CRITICAL(&s_stateMux);
    }
}

bool begin_remote_reset(bool hard) {
    portENTER_CRITICAL(&s_stateMux);
    const bool allowed = !s_configSaving && !s_resetPending && s_appliedRevision == s_configRevision;
    if (allowed) s_resetPending = true;
    portEXIT_CRITICAL(&s_stateMux);
    if (!allowed) return false;

    ocpp_command_t command{};
    command.type = ocpp_command_type_t::Reset;
    command.requestId = ++nextAuthorizationRequestId;
    if (!command.requestId) command.requestId = ++nextAuthorizationRequestId;
    command.connectorId = OCPP_CONNECTOR_ID;
    command.hardReset = hard;
    if (xQueueSend(s_commandQueue, &command, 0) != pdPASS) {
        portENTER_CRITICAL(&s_stateMux);
        s_resetPending = false;
        portEXIT_CRITICAL(&s_stateMux);
        return false;
    }
    remoteReset.hard = hard;
    remoteReset.requestId = command.requestId;
    remoteReset.rpcId = incomingResetRpcId;
    remoteReset.requestedAtMs = millis();
    remoteReset.lastCheckMs = millis() - 250;
    remoteReset.lastWarningMs = millis();
    if (authorizationTx) endTransaction_authorized(nullptr, hard ? "HardReset" : "SoftReset");
    log_event("%s reset accepted; awaiting owner shutdown and stored session", hard ? "Hard" : "Soft");
    return true;
}

class RemoteResetOperation : public MicroOcpp::Operation {
    bool accepted = false;
public:
    const char* getOperationType() override { return "Reset"; }
    void processReq(JsonObject payload) override {
        const bool hard = ocpp_policy::string_equals(payload["type"], "Hard");
        if (!hard && !ocpp_policy::string_equals(payload["type"], "Soft")) return;
        if (incomingResetRpcId.isEmpty() || !stack_is_authoritative()) return;
        // A duplicate RPC neither requeues the reset nor restarts its timer.
        accepted = remoteReset.requestId ?
            remoteReset.rpcId == incomingResetRpcId && remoteReset.hard == hard : begin_remote_reset(hard);
        if (!accepted) log_event("Reset rejected: invalid, busy or stack not ready");
    }
    std::unique_ptr<MicroOcpp::JsonDoc> createConf() override {
        auto doc = MicroOcpp::makeJsonDoc("InnoCharge.reset", 128);
        (*doc)["status"] = accepted ? "Accepted" : "Rejected";
        return doc;
    }
};

void service_remote_reset() {
    if (!remoteReset.requestId) return;
    const uint32_t now = millis();
    if (uint32_t(now - remoteReset.lastCheckMs) < 250) return;
    remoteReset.lastCheckMs = now;
    // mocpp_loop() may just have received Start/Stop.conf in this same iteration.
    // Persist that new ID/confirmation before considering a deadline-triggered reboot.
    service_backend_authorization();
    const uint32_t elapsed = now - remoteReset.requestedAtMs;
    // Leave time for Reset.conf and the existing 500 ms contactor shutdown.
    // A failed network may delay delivery, but not reboot forever: known-ID
    // stops stay in the journal and are retried by the existing boot recovery.
    const bool deliveryWaitExpired = elapsed >= (remoteReset.hard ? 5000U : 30000U);
    const char* waiting = nullptr;
    if (!remoteReset.ownerPrepared || !charging_reset_in_progress()) waiting = "charging owner";
    else if (chargeAuthSession.authorized || stateRelayL1N || stateRelayL2L3) waiting = "contactor outputs off";
    else if (charge_session_log_has_active_session()) waiting = "local session close";
    else if (elapsed < 2000 || (!remoteReset.responseSent && !deliveryWaitExpired)) return;
    else if (!deliveryWaitExpired && (authorizationTx || recoveryStartLocalId || recoveryStopTransactionId >= 0)) return;
    else if (!charge_session_log_flush()) waiting = "durable session storage";
    else {
        // No new grant is possible; storage/output failures never time out into a forced reset.
        log_event("%s reset: outputs off, journal saved; rebooting%s",
            remoteReset.hard ? "Hard" : "Soft", deliveryWaitExpired ? " (delivery recovery after boot)" : "");
        ESP.restart();
        return;
    }
    if (uint32_t(now - remoteReset.lastWarningMs) >= 5000) {
        remoteReset.lastWarningMs = now;
        log_event("Reset waiting for %s; charging remains inhibited", waiting);
    }
}

class SessionRemoteOperation : public MicroOcpp::Operation {
    bool start;
    bool accepted = false;
public:
    explicit SessionRemoteOperation(bool start) : start(start) {}
    const char* getOperationType() override {
        return start ? "RemoteStartTransaction" : "RemoteStopTransaction";
    }
    void processReq(JsonObject payload) override {
        if (start) {
            const char* id = payload["idTag"] | "";
            const JsonString tag = payload["idTag"].as<JsonString>();
            if (!*id || strlen(id) > OCPP_ID_TAG_MAX_LEN || tag.size() != strlen(id) ||
                !is_printable_ascii(id, false) || !payload["chargingProfile"].isNull() ||
                (!payload["connectorId"].isNull() &&
                 (!payload["connectorId"].is<int>() || payload["connectorId"].as<int>() != OCPP_CONNECTOR_ID))) return;
            accepted = begin_backend_authorization(id, true);
        } else if (rfidAuth.required && authorizationTx && owner_session_matches() &&
                   payload["transactionId"].is<int>() && authorizationTx->getStartSync().isConfirmed() &&
                   authorizationTx->getTransactionId() >= 0 &&
                   payload["transactionId"].as<int>() == authorizationTx->getTransactionId()) {
            accepted = enqueue_session_stop(ownerSessionStamp, "Remote");
            if (accepted) {
                endTransaction_authorized(nullptr, "Remote");
                log_event("RemoteStop accepted; revoking existing session");
            }
        }
        if (!accepted) log_event("%s rejected", getOperationType());
    }
    std::unique_ptr<MicroOcpp::JsonDoc> createConf() override {
        auto doc = MicroOcpp::makeJsonDoc("InnoCharge.session", 128);
        (*doc)["status"] = accepted ? "Accepted" : "Rejected";
        return doc;
    }
};

std::shared_ptr<MaxPowerChange> request_max_power_change(JsonVariantConst value) {
    uint16_t tenths = 0;
    const JsonString text = value.as<JsonString>();
    if (pendingMaxPowerChange || !value.is<const char*>() ||
        !ocpp_policy::parse_max_charging_power(text.c_str(), text.size(), tenths)) {
        log_event("Max. Charging Power rejected: busy or invalid kW value (4.2-22.0, step 0.1)");
        return nullptr;
    }
    auto change = std::make_shared<MaxPowerChange>();
    ocpp_command_t command{};
    command.type = ocpp_command_type_t::SetMaxChargingPower;
    command.requestId = ++nextAuthorizationRequestId;
    if (!command.requestId) command.requestId = ++nextAuthorizationRequestId;
    command.expiresAtMs = millis() + 5000;
    command.maxChargingPowerTenths = tenths;
    change->requestId = command.requestId;
    if (xQueueSend(s_commandQueue, &command, 0) != pdPASS) {
        log_event("Max. Charging Power rejected: command queue full");
        return nullptr;
    }
    pendingMaxPowerChange = change;
    return change;
}

class BackendConfigurationOperation : public MicroOcpp::Operation {
    const char* status = "Rejected";
    std::shared_ptr<MaxPowerChange> maxPowerChange;
public:
    const char* getOperationType() override { return "ChangeConfiguration"; }
    void processReq(JsonObject payload) override {
        if (!stack_is_authoritative() || s_resetPending) return;
        const char* key = payload["key"] | "";
        if (!*key || !ocpp_policy::string_equals(payload["key"], key)) return;
        if (!strcmp(key, ocpp_policy::MAX_CHARGING_POWER_KEY)) {
            maxPowerChange = request_max_power_change(payload["value"]);
            return;
        }
        const auto interval = ocpp_policy::interval_key(key);
        const auto retry = ocpp_policy::retry_key(key);
        auto setting = MicroOcpp::getConfigurationPublic(key);
        if (!setting) { status = "NotSupported"; return; }
        if ((interval == ocpp_policy::Interval::Unsupported && retry == ocpp_policy::RetryKey::Unsupported) ||
            !payload["value"].is<const char*>()) return;
        const JsonString value = payload["value"].as<JsonString>();
        uint32_t parsed = 0;
        if (retry != ocpp_policy::RetryKey::Unsupported) {
            if (!ocpp_policy::parse_retry(retry, value.c_str(), value.size(), parsed)) return;
        } else if (!ocpp_policy::parse_interval(interval, value.c_str(), value.size(), parsed)) return;
        BackendSettings next = backendSettings;
        if (retry == ocpp_policy::RetryKey::Attempts) next.transactionMessageAttempts = parsed;
        else if (retry == ocpp_policy::RetryKey::Interval) next.transactionMessageRetrySeconds = parsed;
        else if (interval == ocpp_policy::Interval::Heartbeat) next.heartbeatSeconds = parsed;
        else next.meterSeconds = parsed;
        if (!save_backend_settings(next)) return;
        backendSettings = next;
        setting->setInt(int(parsed));
        publish_backend_settings();
        status = "Accepted";
        log_event("ChangeConfiguration: %s=%lu", key, (unsigned long)parsed);
    }
    std::unique_ptr<MicroOcpp::JsonDoc> createConf() override {
        if (maxPowerChange && !maxPowerChange->completed) return nullptr;
        if (maxPowerChange) status = maxPowerChange->saved ? "Accepted" : "Rejected";
        auto doc = MicroOcpp::makeJsonDoc("InnoCharge.config", 128);
        (*doc)["status"] = status;
        return doc;
    }
};

class BackendAvailabilityOperation : public MicroOcpp::Operation {
    const char* status = "Rejected";
public:
    const char* getOperationType() override { return "ChangeAvailability"; }
    void processReq(JsonObject payload) override {
        if (!stack_is_authoritative() || s_resetPending || !payload["connectorId"].is<int>()) return;
        const int connector = payload["connectorId"].as<int>();
        if (connector != 0 && connector != OCPP_CONNECTOR_ID) return;
        const bool operative = ocpp_policy::string_equals(payload["type"], "Operative");
        if (!operative && !ocpp_policy::string_equals(payload["type"], "Inoperative")) return;
        BackendSettings next = backendSettings;
        next.operative = operative ? 1 : 0;
        if (!save_backend_settings(next)) return;
        backendSettings = next;
        publish_backend_settings();
        const bool active = chargeAuthSession.authorized || charge_session_log_has_active_session();
        status = !operative && active ? "Scheduled" : "Accepted";
        // Deliberate product policy: stop NEW OCPP grants/reporting, never local RFID.
        // This is not a station-wide maintenance lock. Existing sessions may finish.
        log_event("OCPP availability %s; local RFID unchanged (%s)", operative ? "ON" : "OFF", status);
    }
    std::unique_ptr<MicroOcpp::JsonDoc> createConf() override {
        auto doc = MicroOcpp::makeJsonDoc("InnoCharge.availability", 128);
        (*doc)["status"] = status;
        return doc;
    }
};

void configure_charger_stack() {
    auto& context = *getOcppContext();
    // MicroOCPP installs an ESP restart callback by default. Disable it before
    // processing the first server frame. Only our owner-mediated reset may reboot.
    setOnResetNotify([](bool) { return false; });
    setOnResetExecute([](bool) {});
    // Protocol availability only; physical permission remains the existing session.
    for (unsigned int id = 0; id < context.getModel().getNumConnectors(); ++id)
        context.getModel().getConnector(id)->setAvailabilityVolatile(true);
    setConnectorPluggedInput([] { return reportingExistingSession || vCurrentCpState.vehicleConnected ||
        (owner_session_matches() && (switchToL1N || switchToL2L3)); });
    setStartTxReadyInput([] { return (reportingExistingSession || owner_session_matches()) &&
        authorizationTx && startJournalReady; });
    setStopTxReadyInput([] { return !owner_session_matches() && authorizationTx &&
        authorizationTx->isMeterStopDefined() && stopJournalReady; });
    context.getOperationRegistry().registerOperation("RemoteStartTransaction", [] {
        return new SessionRemoteOperation(true);
    });
    context.getOperationRegistry().registerOperation("RemoteStopTransaction", [] {
        return new SessionRemoteOperation(false);
    });
    context.getOperationRegistry().registerOperation("Reset", [] {
        return new RemoteResetOperation();
    });
    context.getOperationRegistry().registerOperation("ChangeConfiguration", [] {
        return new BackendConfigurationOperation();
    });
    context.getOperationRegistry().registerOperation("ChangeAvailability", [] {
        return new BackendAvailabilityOperation();
    });

    struct Blocked { const char* action; const char* status; };
    const Blocked blocked[] = {
        {"UnlockConnector", "NotSupported"},
        {"ClearCache", "Rejected"}, {"ReserveNow", "Rejected"},
        {"CancelReservation", "Rejected"}, {"SendLocalList", "NotSupported"},
        {"GetLocalListVersion", nullptr},
        {"SetChargingProfile", "NotSupported"}, {"ClearChargingProfile", "Unknown"},
        {"GetCompositeSchedule", "Rejected"}, {"UpdateFirmware", nullptr},
        {"GetDiagnostics", nullptr}, {"DataTransfer", "UnknownVendorId"},
        // Echo-server helper operations must not accept station commands from CSMS.
        {"Authorize", nullptr}, {"StartTransaction", nullptr}, {"StopTransaction", nullptr}
    };
    for (const auto& op : blocked) {
        context.getOperationRegistry().registerOperation(op.action, [op] {
            return new TestBlockedOperation(op.action, op.status);
        });
    }
    context.getOperationRegistry().registerOperation("TriggerMessage", [&context] {
        return new TestTriggerMessage(context);
    });
    // Disable unused optional services after replacing their captured callbacks.
    // Thus the stack does not advertise Reservation / LocalAuthListManagement.
#if MO_ENABLE_LOCAL_AUTH
    context.getModel().setAuthorizationService(nullptr);
#endif
#if MO_ENABLE_RESERVATION
    context.getModel().setReservationService(nullptr);
#endif
    MicroOcpp::declareConfiguration<int>("HeartbeatInterval", 120)->setInt(backendSettings.heartbeatSeconds);
    MicroOcpp::declareConfiguration<int>("ConnectionTimeOut", 30)->setInt(120);
    MicroOcpp::declareConfiguration<int>("TransactionMessageAttempts", 3)
        ->setInt(backendSettings.transactionMessageAttempts);
    MicroOcpp::declareConfiguration<int>("TransactionMessageRetryInterval", 60)
        ->setInt(backendSettings.transactionMessageRetrySeconds);
    // No library meter samplers: the adapter sends only complete, fresh cycles.
    MicroOcpp::declareConfiguration<int>("MeterValueSampleInterval", 60,
        CONFIGURATION_VOLATILE)->setInt(backendSettings.meterSeconds);
    // Read the same cap as the Web UI, including later local changes. NVS stays
    // exclusively with the existing set_max_charging_power() storage path.
    MicroOcpp::declareConfiguration<const char*>(ocpp_policy::MAX_CHARGING_POWER_KEY, "",
        CONFIGURATION_VOLATILE);
    setOnReceiveRequest("GetConfiguration", [](JsonObject) {
        char value[12];
        snprintf(value, sizeof(value), "%.1f", get_max_charging_power() / 10.0f);
        auto setting = MicroOcpp::getConfigurationPublic(ocpp_policy::MAX_CHARGING_POWER_KEY);
        if (setting) setting->setString(value);
    });
    MicroOcpp::declareConfiguration<const char*>("MeterValuesSampledData",
        "Energy.Active.Import.Register,Power.Active.Import", CONFIGURATION_VOLATILE, true)
        ->setString("Energy.Active.Import.Register,Power.Active.Import");
    MicroOcpp::declareConfiguration<int>("ClockAlignedDataInterval", 0)->setInt(0);
    MicroOcpp::declareConfiguration<const char*>("StopTxnSampledData", "")->setString("");
    MicroOcpp::declareConfiguration<const char*>("StopTxnAlignedData", "")->setString("");
    MicroOcpp::declareConfiguration<bool>("StopTransactionOnInvalidId", true)->setBool(true);
    MicroOcpp::declareConfiguration<bool>("AuthorizeRemoteTxRequests", false)->setBool(false);
    MicroOcpp::declareConfiguration<bool>("AllowOfflineTxForUnknownId", false)->setBool(false);
    // Only the existing session owner ends a session, never a CP phase-switch pause.
    MicroOcpp::declareConfiguration<bool>("StopTransactionOnEVSideDisconnect", false)->setBool(false);
    // GetConfiguration must describe the same allow-list as ChangeConfiguration.
    for (auto container : MicroOcpp::getConfigurationContainersPublic()) {
        for (size_t i = 0; i < container->size(); ++i) {
            auto setting = container->getConfiguration(i);
            if (setting->getKey() && strcmp(setting->getKey(), ocpp_policy::MAX_CHARGING_POWER_KEY) &&
                ocpp_policy::interval_key(setting->getKey()) == ocpp_policy::Interval::Unsupported &&
                ocpp_policy::retry_key(setting->getKey()) == ocpp_policy::RetryKey::Unsupported)
                setting->setReadOnly();
        }
    }
}

bool start_test_stack(const ocpp_config_t& config, WebSocketsClient& socket,
                      TestConnection& connection, const char* caPem) {
    const bool tls = config.securityProfile == ocpp_security_profile_t::TlsBasicAuthentication;
    String host, path;
    uint16_t port;
    if (!parse_test_endpoint(config.backendUrl, host, port, path)) return false;
    if (!path.endsWith("/")) path += '/';
    path += config.chargePointId; // add exactly once; configured URL is the base
    if (tls) {
        String error;
        if (!time_service_is_valid() || !validate_ca(caPem, error)) {
            set_last_error(error.isEmpty() ? "TLS requires a valid device clock" : error.c_str());
            return false;
        }
        // CA memory belongs to the task and outlives the socket. Never use
        // beginSSL without a CA: WebSockets 2.4.1 otherwise calls setInsecure.
        socket.beginSslWithCA(host.c_str(), port, path.c_str(), caPem, "ocpp1.6");
        if (strlen(config.authorizationKey) <= ocpp_auth::MAX_PASSWORD_BYTES) {
            // Preserve the existing, tested plaintext authentication path exactly.
            socket.setAuthorization(config.chargePointId, config.authorizationKey);
        } else {
            uint8_t credentials[OCPP_CHARGE_POINT_ID_MAX_LEN + ocpp_auth::MAX_PASSWORD_BYTES]{};
            const size_t userLength = strlen(config.chargePointId);
            memcpy(credentials, config.chargePointId, userLength);
            credentials[userLength] = ':';
            size_t passwordLength = 0;
            bool valid = ocpp_auth::decode_password(config.authorizationKey,
                credentials + userLength + 1, passwordLength);
            char header[6 + 4 * ((sizeof(credentials) + 2) / 3) + 1] = "Basic ";
            size_t encodedLength = 0;
            valid = valid && mbedtls_base64_encode(reinterpret_cast<unsigned char*>(header + 6),
                sizeof(header) - 6, &encodedLength, credentials, userLength + 1 + passwordLength) == 0;
            if (valid) {
                header[6 + encodedLength] = '\0';
                socket.setAuthorization(header);
            }
            mbedtls_platform_zeroize(credentials, sizeof(credentials));
            mbedtls_platform_zeroize(header, sizeof(header));
            if (!valid) { set_last_error("Cannot prepare HTTP Basic authentication"); return false; }
        }
    } else {
        socket.begin(host, port, path, "ocpp1.6");
        // Each revision creates a fresh socket; no Basic credentials on WS.
    }
    socket.setReconnectInterval(5000);
    socket.enableHeartbeat(30000, 10000, 2);
    ChargerCredentials credentials(config.chargePointModel, config.chargePointVendor,
                                   config.firmwareVersion,
                                   config.chargePointSerialNumber[0] ? config.chargePointSerialNumber : nullptr);
    // No filesystem adapter: do not mount/format LittleFS over the UI SPIFFS.
    // MicroOCPP retries live transactions in RAM. Our existing journal alone owns
    // reboot recovery (original Start -> returned ID -> existing Stop replay).
    mocpp_initialize(connection, credentials, nullptr, false, MicroOcpp::ProtocolVersion(1, 6));
    if (!getOcppContext()) return false;
    configure_charger_stack();
    log_event(tls ? "Starting OCPP 1.6-J / Profile 2; TLS CA verification required" :
                   "Starting OCPP 1.6-J / Profile 0; unencrypted connection");
    return true;
}

} // namespace

void ocpp_local_settings_begin() {
    LocalRfidSettings saved;
    Preferences store;
    if (store.begin("localRfid", true)) {
        if (store.getBytesLength("settings") == sizeof(saved) &&
            store.getBytes("settings", &saved, sizeof(saved)) == sizeof(saved) &&
            saved.magic == 0x4C524601 && saved.report <= 2 &&
            memchr(saved.customId, '\0', sizeof(saved.customId)) &&
            (saved.report != 2 || (*saved.customId && is_printable_ascii(saved.customId, false)))) {
            portENTER_CRITICAL(&s_stateMux); s_localRfid = saved; portEXIT_CRITICAL(&s_stateMux);
        }
        store.end();
    }
}

bool ocpp_local_rfid_enabled() {
    portENTER_CRITICAL(&s_stateMux);
    const bool enabled = s_localRfid.enabled;
    portEXIT_CRITICAL(&s_stateMux);
    return enabled;
}

bool ocpp_backend_accepts_cards() {
    portENTER_CRITICAL(&s_stateMux);
    const bool allowed = s_config.enabled && s_backendOperative && !s_resetPending;
    portEXIT_CRITICAL(&s_stateMux);
    return allowed;
}

void ocpp_prepare_session(uint32_t stamp, const char* tag, bool backendAuthorized) {
    session_ocpp_link_t link;
    portENTER_CRITICAL(&s_stateMux);
    if (s_config.enabled && s_backendOperative && (backendAuthorized || s_localRfid.report != 0)) {
        link.local = !backendAuthorized;
        link.start = session_ocpp_state_t::Pending;
        copy_text(link.idTag, sizeof(link.idTag), !backendAuthorized && s_localRfid.report == 2 ?
            s_localRfid.customId : tag);
        copy_text(link.backendUrl, sizeof(link.backendUrl), s_config.backendUrl);
        copy_text(link.chargePointId, sizeof(link.chargePointId), s_config.chargePointId);
    }
    portEXIT_CRITICAL(&s_stateMux);
    // RAM-only metadata capture before permission is published. No backend wait.
    charge_session_log_prepare_ocpp(stamp, link);
}

bool ocpp_ui_save_local_rfid(JsonObjectConst settings, String& error) {
    if (!settings["enabled"].is<bool>() || !settings["mode"].is<const char*>() ||
        !settings["customId"].is<const char*>()) { error = "Invalid RFID settings."; return false; }
    const char* mode = settings["mode"];
    const char* custom = settings["customId"];
    LocalRfidSettings saved{};
    saved.enabled = settings["enabled"].as<bool>();
    if (!strcmp(mode, "none")) saved.report = 0;
    else if (!strcmp(mode, "actual")) saved.report = 1;
    else if (!strcmp(mode, "custom")) saved.report = 2;
    else { error = "Invalid reporting mode."; return false; }
    if (strlen(custom) > 20 || settings["customId"].as<JsonString>().size() != strlen(custom) ||
        (*custom && !is_printable_ascii(custom, false)) || (saved.report == 2 && !*custom)) {
        error = "Custom ID: 1-20 ASCII characters, no spaces."; return false;
    }
    copy_text(saved.customId, sizeof(saved.customId), custom);
    Preferences store;
    if (!store.begin("localRfid", false)) { error = "RFID storage unavailable."; return false; }
    const bool ok = store.putBytes("settings", &saved, sizeof(saved)) == sizeof(saved);
    store.end();
    if (!ok) { error = "RFID settings not saved."; return false; }
    portENTER_CRITICAL(&s_stateMux); s_localRfid = saved; portEXIT_CRITICAL(&s_stateMux);
    log_event("Local RFID settings saved; apply to next authorization");
    return true;
}

void ocpp_ui_add_local_rfid(JsonObject state) {
    LocalRfidSettings saved;
    portENTER_CRITICAL(&s_stateMux); saved = s_localRfid; portEXIT_CRITICAL(&s_stateMux);
    state["localRfidControlsAvailable"] = true;
    state["localRfidEnabled"] = saved.enabled;
    state["localRfidOcppReporting"] = saved.report == 2 ? "custom" : saved.report == 1 ? "actual" : "none";
    state["localRfidCustomOcppId"] = saved.customId;
}

static bool apply_config(const ocpp_config_t& config, const char* caPem) {
    const char* error = nullptr;
    if (!validate_config(config, &error)) {
        set_last_error(error);
        return false;
    }

    portENTER_CRITICAL(&s_stateMux);
    if (s_authorizationBusy || s_resetPending) {
        portEXIT_CRITICAL(&s_stateMux);
        set_last_error("Wait for OCPP session completion / pending reset before changing settings");
        return false;
    }
    memset(s_config.authorizationKey, 0, sizeof(s_config.authorizationKey));
    s_config = config;
    ++s_latestRfidRequestId;
    s_status.rfidRejected = false;
    s_status.rfidRejectedTag[0] = '\0';
    // Disabled means stop transport, not forget saved credentials.
    if (caPem) copy_text(s_caPem, sizeof(s_caPem), caPem);
    s_configAvailable = true;
    ++s_configRevision;
    TaskHandle_t task = s_taskHandle;
    set_last_error_locked("");
    if (!config.enabled && !s_status.stackInitialized) {
        s_status.connectionState = ocpp_connection_state_t::Disabled;
    } else if (!s_status.taskInitialized) {
        s_status.connectionState =
            ocpp_connection_state_t::ConfiguredNotStarted;
    }
    portEXIT_CRITICAL(&s_stateMux);
    if (task) xTaskNotifyGive(task);
    return true;
}

bool ocpp_set_config(const ocpp_config_t& config) {
    return apply_config(config, nullptr);
}

bool ocpp_present_id_tag(const char* idTag, TickType_t timeoutTicks) {
    // Clear the previous indication even if the new request cannot be queued
    // (offline, busy, invalid tag). Its old backend reply must not turn it red.
    portENTER_CRITICAL(&s_stateMux);
    if (!++s_latestRfidRequestId) ++s_latestRfidRequestId;
    const uint32_t requestId = s_latestRfidRequestId;
    s_status.rfidRejected = false;
    s_status.rfidRejectedTag[0] = '\0';
    portEXIT_CRITICAL(&s_stateMux);
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

    if (!stack_is_authoritative() || !ocpp_backend_accepts_cards()) {
        return false;
    }

    input_event_t event{};
    event.type = input_event_type_t::IdTagPresented;
    event.createdAtMs = millis();
    event.rfidRequestId = requestId;
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
        set_last_error("OCPP backend is unavailable");
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

    // Owner acknowledgements must also arrive during an offline interval.
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

    if (!queue || xQueueReceive(queue, outCommand, timeoutTicks) != pdPASS) return false;
    if (outCommand->type == ocpp_command_type_t::GrantSession) {
        portENTER_CRITICAL(&s_stateMux);
        const bool current = outCommand->requestId == s_grantRequestId;
        portEXIT_CRITICAL(&s_stateMux);
        if (!current) return false;
    }
    return true;
}

bool ocpp_get_runtime_status(ocpp_runtime_status_t* outStatus) {
    if (outStatus == nullptr) {
        return false;
    }

    portENTER_CRITICAL(&s_stateMux);
    *outStatus = s_status;
    outStatus->enabled = s_config.enabled;
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
    s_taskHandle = xTaskGetCurrentTaskHandle();
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
        s_taskHandle = nullptr;
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

    ocpp_config_t config{};
    uint32_t revision = 0;
    // Allocate outside the task stack; the library borrows this CA pointer.
    std::unique_ptr<char[]> activeCa(new (std::nothrow) char[CA_PEM_MAX + 1]{});
    if (!activeCa) {
        set_last_error("Cannot allocate OCPP CA buffer; reboot to retry");
        portENTER_CRITICAL(&s_stateMux);
        s_status.taskInitialized = false;
        s_status.connectionState = ocpp_connection_state_t::Faulted;
        portEXIT_CRITICAL(&s_stateMux);
        for (;;) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    std::unique_ptr<OcppSocket> socket;
    std::unique_ptr<TestConnection> connection;
    bool initialized = false;
    bool initFailed = false;
    bool wasConnected = false;
    bool connectedOnce = false;
    uint32_t lastSummaryMs = 0;
    log_event("OCPP controller ready; charger control remains local");
    while (true) {
        ocpp_config_t requested{};
        portENTER_CRITICAL(&s_stateMux);
        const uint32_t requestedRevision = s_configRevision;
        if (revision != requestedRevision) {
            requested = s_config;
            // Only this task uses the old socket, and it is torn down below
            // before any further loop/connect call uses the new CA contents.
            memcpy(activeCa.get(), s_caPem, CA_PEM_MAX + 1);
        }
        portEXIT_CRITICAL(&s_stateMux);
        if (revision != requestedRevision) {
            // Only this task accesses the socket/stack, including destruction.
            if (socket) {
                socket->disconnect();
                socket->onEvent(nullptr); // Drop callbacks before their owners.
            }
            if (getOcppContext()) mocpp_deinitialize();
            connection.reset();
            socket.reset();
            initialized = false;
            initFailed = false;
            wasConnected = false;
            connectedOnce = false;
            pendingStatus = ChargePointStatus_UNDEFINED;
            pendingErrorCode[0] = '\0';
            pendingStatusSinceMs = 0;
            confirmedStatus = ChargePointStatus_UNDEFINED;
            confirmedErrorCode[0] = '\0';
            chargingSeenSinceUnplug = false;
            statusRevision = 1;
            acknowledgedStatusRevision[0] = acknowledgedStatusRevision[1] = 0;
            statusRequestPending = false;
            lastStatusAttemptMs = millis() - 5000;
            MeterValid = false;
            meterRequestPending = meterTriggerPending = meterSentOnce = false;
            lastMeterAttemptMs = 0;
            recoveryStopTransactionId = -1;
            recoveryStartLocalId = 0;
            recoveryStartFrame = false;
            lastRecoveryStartAttemptMs = millis() - RECOVERY_STOP_RETRY_MS;
            lastRecoveryStopAttemptMs = millis() - RECOVERY_STOP_RETRY_MS;
            xQueueReset(eventQueue);
            xQueueReset(commandQueue);
            pendingMaxPowerChange.reset();
            config = requested;
            load_backend_settings(config); // Also restore an operator block while network is offline.
            revision = requestedRevision;
            portENTER_CRITICAL(&s_stateMux);
            s_status.stackInitialized = false;
            s_status.transactionActive = false;
            s_status.transactionRunning = false;
            s_status.chargePermitted = false;
            s_status.websocketConnected = false;
            s_status.tlsVerified = false;
            s_status.bootAccepted = false;
            s_status.lastMessageTimeMs = 0;
            s_status.heartbeatConfirmations = 0;
            s_status.lastHeartbeatTimeMs = 0;
            s_status.reconnectCount = 0;
            s_status.connectionState = config.enabled ?
                ocpp_connection_state_t::WaitingForNetwork : ocpp_connection_state_t::Disabled;
            s_appliedRevision = revision;
            portEXIT_CRITICAL(&s_stateMux);
            log_event(config.enabled ? "OCPP enabled; applying saved settings" :
                "OCPP disabled; stack/socket released, controller sleeping");
        }
        if (!config.enabled || initFailed) {
            // No reconnects, MicroOCPP loops, polling or periodic OCPP logs.
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        input_event_t event{};
        if (xQueueReceive(eventQueue, &event, 0) == pdPASS) {
            process_authorization_event(event);
        }
        const bool online = network_has_address();
        const bool utcValid = time_service_is_valid();
        const bool tls = config.securityProfile == ocpp_security_profile_t::TlsBasicAuthentication;
        const bool waitingForTime = tls && !utcValid;
        if (waitingForTime && initialized && socket->isConnected()) socket->disconnect();
        if (online && !waitingForTime && !initialized) {
            socket.reset(new (std::nothrow) OcppSocket());
            if (socket) connection.reset(new (std::nothrow) TestConnection(socket.get()));
            if (!connection) set_last_error("Insufficient memory for OCPP stack");
            initialized = connection && start_test_stack(config, *socket, *connection, activeCa.get());
            initFailed = !initialized;
            if (initialized) set_last_error("");
            else {
                if (socket) {
                    socket->disconnect();
                    socket->onEvent(nullptr);
                }
                if (getOcppContext()) mocpp_deinitialize();
                connection.reset();
                socket.reset();
                log_event("OCPP initialization failed; change settings or disable/enable to retry");
            }
        }
        if (initialized) {
            ocpp_meter_sample_t meter{};
            portENTER_CRITICAL(&ocppMeterMux);
            meter = ocppMeterSample;
            portEXIT_CRITICAL(&ocppMeterMux);

            // Status zu OCPP-Server - ANFANG
            //
            // Reservierungen sind noch nicht implementiert.
            Reserved = false;

            Faulted =
                vCurrentCpState.state == StateE_Error ||
                vCurrentCpState.state == StateF_Fault ||
                vCurrentCpState.state == StateCustom_InvalidValue ||
                vCurrentCpState.state == StateCustom_OutOffRange;

            // State F entsteht im bestehenden CP-Code durch den RCM-Fehler-Latch.
            ErrorCode =
                vCurrentCpState.state == StateF_Fault ? "GroundFailure" :
                vCurrentCpState.state == StateE_Error ? "EVCommunicationError" :
                Faulted ? "OtherError" : "NoError";

            // CP abgeschaltet / kein erkennbarer Anschlusszustand.
            // Eine Phasenpause ist keine Ausserbetriebnahme.
            Unavailable =
                !Faulted && !Reserved &&
                !switchToL1N && !switchToL2L3 &&
                (vCurrentCpState.state == StateCustom_CpRelayOff ||
                 (!vCurrentCpState.vehicleConnected &&
                  vCurrentCpState.state != StateA_NotConnected));
            // Product policy: local sessions still run; only idle OCPP availability is withdrawn.
            if (!backendSettings.operative && !chargeAuthSession.authorized &&
                !charge_session_log_has_active_session() && !Faulted && !Reserved) Unavailable = true;

            // Bereits geladen, lokale Freigabe beendet, Schuetze offen,
            // Fahrzeug aber noch angesteckt. Keine Ableitung aus PWM-Pausen.
            Finishing =
                !Faulted && !Unavailable && !Reserved &&
                chargingSeenSinceUnplug && vCurrentCpState.vehicleConnected &&
                rfidAuth.required && !chargeAuthSession.authorized &&
                !switchToL1N && !switchToL2L3 &&
                !stateRelayL1N && !stateRelayL2L3;

            // CP C/D allein reicht nicht: Freigabe, PWM und Relais beruecksichtigen.
            // gridProtectionStatus 2/3/4 begrenzen ggf. die Leistung, stoppen aber nicht.
            Charging =
                !Faulted && !Unavailable && !Reserved && !Finishing &&
                vCurrentCpState.vehicleConnected && vCurrentCpState.chargingActive &&
                (!rfidAuth.required || chargeAuthSession.authorized) &&
                !switchToL1N && !switchToL2L3 && gridProtectionStatus != 1 &&
                getCpDuty >= 10.0f && getCpDuty <= 96.0f &&
                stateRelayL1N && (!threePhaseActive || stateRelayL2L3);

            SuspendedEVSE =
                !Faulted && !Unavailable && !Reserved && !Finishing && !Charging &&
                (vCurrentCpState.vehicleConnected ||
                 (chargingSeenSinceUnplug && (switchToL1N || switchToL2L3))) &&
                (chargingSeenSinceUnplug || chargeAuthSession.authorized) &&
                (switchToL1N || switchToL2L3 || gridProtectionStatus == 1 ||
                 (rfidAuth.required && !chargeAuthSession.authorized) ||
                 !(getCpDuty >= 10.0f && getCpDuty <= 96.0f) ||
                 (vCurrentCpState.chargingActive &&
                  (!stateRelayL1N || (threePhaseActive && !stateRelayL2L3))));

            SuspendedEV =
                !Faulted && !Unavailable && !Reserved && !Finishing &&
                !Charging && !SuspendedEVSE &&
                vCurrentCpState.vehicleConnected && !vCurrentCpState.chargingActive &&
                (!rfidAuth.required || chargeAuthSession.authorized) &&
                (chargingSeenSinceUnplug || chargeAuthSession.authorized) &&
                !switchToL1N && !switchToL2L3 && gridProtectionStatus != 1 &&
                getCpDuty >= 10.0f && getCpDuty <= 96.0f;

            Preparing =
                !Faulted && !Unavailable && !Reserved && !Finishing &&
                !Charging && !SuspendedEVSE && !SuspendedEV &&
                (vCurrentCpState.vehicleConnected || chargeAuthSession.authorized);

            Available =
                (vCurrentCpState.state == StateA_NotConnected) &&
                !chargeAuthSession.authorized &&
                !switchToL1N && !switchToL2L3 &&
                !Faulted && !Unavailable && !Reserved;

            // SDM: kWh -> Wh; Leistung liegt bereits in W vor.
            // Keine alten, unvollstaendigen oder ungueltigen Werte als Messung senden.
            MeterValid = sdm.enable && meter.valid &&
                uint32_t(millis() - meter.capturedAtMs) <= OCPP_METER_MAX_AGE_MS &&
                std::isfinite(meter.importKWh) && meter.importKWh >= 0.0f &&
                double(meter.importKWh) * 1000.0 < 9.22e18 && std::isfinite(meter.powerW);
            EnergyImportWh = MeterValid ? int64_t(std::llround(double(meter.importKWh) * 1000.0)) : 0;
            // Importleistung ist nicht negativ; Rueckspeisung waere ein eigener Measurand.
            PowerImportW = MeterValid && meter.powerW > 0.0f ? meter.powerW : 0.0f;
            meterCapturedAtMs = meter.capturedAtMs;
            //
            //
            // Status zu OCPP-Server - ENDE

            // Gemeinsame 250-ms-Entprellung NACH der Statuszuweisung.
            // Genau ein Status muss aktiv sein; keine versteckte Priorisierung.
            const unsigned int activeStatuses =
                unsigned(Available) + unsigned(Preparing) + unsigned(Charging) +
                unsigned(SuspendedEVSE) + unsigned(SuspendedEV) + unsigned(Finishing) +
                unsigned(Reserved) + unsigned(Unavailable) + unsigned(Faulted);
            const ChargePointStatus currentStatus =
                Available     ? ChargePointStatus_Available :
                Preparing     ? ChargePointStatus_Preparing :
                Charging      ? ChargePointStatus_Charging :
                SuspendedEVSE ? ChargePointStatus_SuspendedEVSE :
                SuspendedEV   ? ChargePointStatus_SuspendedEV :
                Finishing     ? ChargePointStatus_Finishing :
                Reserved      ? ChargePointStatus_Reserved :
                Unavailable   ? ChargePointStatus_Unavailable :
                Faulted       ? ChargePointStatus_Faulted : ChargePointStatus_UNDEFINED;
            const char* currentErrorCode = ErrorCode && *ErrorCode ? ErrorCode : "NoError";
            const uint32_t statusNowMs = millis();

            // Keine eindeutige Auswahl: laufende Wartezeit verwerfen,
            // den zuletzt bestaetigten Status aber nicht veraendern.
            if (activeStatuses != 1 ||
                strnlen(currentErrorCode, sizeof(pendingErrorCode)) >= sizeof(pendingErrorCode)) {
                pendingStatus = ChargePointStatus_UNDEFINED;
                pendingErrorCode[0] = '\0';
                pendingStatusSinceMs = statusNowMs;
            } else if (currentStatus != pendingStatus || strcmp(currentErrorCode, pendingErrorCode)) {
                pendingStatus = currentStatus;
                copy_text(pendingErrorCode, sizeof(pendingErrorCode), currentErrorCode);
                pendingStatusSinceMs = statusNowMs;
            } else if (uint32_t(statusNowMs - pendingStatusSinceMs) >= OCPP_STATUS_DEBOUNCE_MS &&
                       (confirmedStatus != pendingStatus || strcmp(confirmedErrorCode, pendingErrorCode))) {
                confirmedStatus = pendingStatus;
                copy_text(confirmedErrorCode, sizeof(confirmedErrorCode), pendingErrorCode);
                ++statusRevision;
                log_event("Status stable: %s / %s", MicroOcpp::cstrFromOcppEveState(confirmedStatus), confirmedErrorCode);
            }
            // confirmedStatus + confirmedErrorCode sind das entprellte Ergebnis.
            if (confirmedStatus == ChargePointStatus_Charging) {
                chargingSeenSinceUnplug = true;
            } else if (confirmedStatus == ChargePointStatus_Available &&
                       vCurrentCpState.state == StateA_NotConnected &&
                       !switchToL1N && !switchToL2L3) {
                chargingSeenSinceUnplug = false;
            }

            service_backend_authorization();
            if (online && !waitingForTime) {
                mocpp_loop();
                socket->reportFailure();
                if (connection->bootAccepted && !connection->firstHeartbeatQueued) {
                    // Give the first connection test an immediate observable
                    // heartbeat; subsequent intervals belong to the backend.
                    getOcppContext()->initiateRequest(MicroOcpp::makeRequest(
                        new MicroOcpp::Ocpp16::Heartbeat(getOcppContext()->getModel())));
                    connection->firstHeartbeatQueued = true;
                }
                if (connection->bootAccepted && socket->isConnected() && utcValid) {
                    auto& context = *getOcppContext();
                    service_recovered_stop(context, config);
                    service_recovered_start(context, config);
                    // Nur ein eigener Statusauftrag gleichzeitig. Bei schnellen
                    // Wechseln / Offline-Zeiten folgt danach der neueste Status.
                    if (confirmedStatus != ChargePointStatus_UNDEFINED && !statusRequestPending &&
                        uint32_t(millis() - lastStatusAttemptMs) >= 5000) {
                        for (unsigned id = 0; id <= OCPP_CONNECTOR_ID; ++id) {
                            if (acknowledgedStatusRevision[id] == statusRevision) continue;
                            auto request = MicroOcpp::makeRequest(new MicroOcpp::Ocpp16::StatusNotification(
                                id, confirmedStatus, context.getModel().getClock().now()));
                            request->setTimeout(30000);
                            request->setOnReceiveConfListener([](JsonObject) {
                                statusRequestPending = false;
                                lastStatusAttemptMs = millis() - 5000;
                            });
                            request->setOnAbortListener([] {
                                statusRequestPending = false;
                                lastStatusAttemptMs = millis();
                                log_event("StatusNotification not confirmed; retrying");
                            });
                            statusRequestPending = true;
                            lastStatusAttemptMs = millis();
                            context.initiateRequest(std::move(request));
                            break;
                        }
                    }
                    if (MeterValid && sdm.enable && !meterRequestPending &&
                        uint32_t(millis() - meter.capturedAtMs) <= OCPP_METER_MAX_AGE_MS &&
                        (meterTriggerPending || (backendSettings.meterSeconds != 0 &&
                         (!meterSentOnce || uint32_t(millis() - lastMeterAttemptMs) >= backendSettings.meterSeconds * 1000U)))) {
                        const bool triggered = meterTriggerPending;
                        const int64_t energyWh = EnergyImportWh;
                        const float powerW = PowerImportW;
                        const auto sampledAt = context.getModel().getClock().now() -
                            int(uint32_t(millis() - meter.capturedAtMs) / 1000);
                        const int transactionId = authorizationTx && owner_session_matches() &&
                            transactionRecord.active && authorizationTx->isRunning() &&
                            authorizationTx->getStartSync().isConfirmed() ? authorizationTx->getTransactionId() : -1;
                        auto request = MicroOcpp::makeRequest(new MicroOcpp::Ocpp16::CustomOperation(
                            "MeterValues", [energyWh, powerW, sampledAt, triggered, transactionId] {
                                auto doc = MicroOcpp::makeJsonDoc("InnoCharge.meter", 768);
                                (*doc)["connectorId"] = OCPP_CONNECTOR_ID;
                                if (transactionId >= 0) (*doc)["transactionId"] = transactionId;
                                auto value = (*doc)["meterValue"].to<JsonArray>().add<JsonObject>();
                                char timestamp[JSONDATE_LENGTH + 1]{};
                                sampledAt.toJsonString(timestamp, sizeof(timestamp));
                                value["timestamp"] = timestamp;
                                auto samples = value["sampledValue"].to<JsonArray>();
                                char number[48];
                                auto energy = samples.add<JsonObject>();
                                snprintf(number, sizeof(number), "%lld", static_cast<long long>(energyWh));
                                energy["value"] = number;
                                energy["context"] = triggered ? "Trigger" : "Sample.Periodic";
                                energy["measurand"] = "Energy.Active.Import.Register";
                                energy["unit"] = "Wh";
                                auto power = samples.add<JsonObject>();
                                snprintf(number, sizeof(number), "%.1f", double(powerW));
                                power["value"] = number;
                                power["context"] = triggered ? "Trigger" : "Sample.Periodic";
                                power["measurand"] = "Power.Active.Import";
                                power["unit"] = "W";
                                return doc;
                            }, [](JsonObject) {
                                meterRequestPending = false;
                                log_event("MeterValues confirmed");
                            }));
                        request->setTimeout(10000);
                        request->setOnAbortListener([] {
                            meterRequestPending = false;
                            log_event("MeterValues not confirmed; next sample will retry");
                        });
                        meterRequestPending = true;
                        meterTriggerPending = false;
                        meterSentOnce = true;
                        lastMeterAttemptMs = millis();
                        context.initiateRequest(std::move(request));
                    }
                }
            } else if (socket->isConnected()) {
                socket->disconnect();
            }
        }
        service_remote_reset();
        const bool connected = initialized && online && socket->isConnected();
        const uint32_t nowMs = millis();
        portENTER_CRITICAL(&s_stateMux);
        s_status.stackInitialized = initialized;
        s_status.networkReady = online;
        s_status.timeValid = utcValid;
        s_status.websocketConnected = connected;
        s_status.tlsVerified = tls && connected;
        s_status.bootAccepted = connection && connection->bootAccepted;
        s_status.transactionActive = authorizationTx && authorizationTx->isActive();
        s_status.transactionRunning = authorizationTx && authorizationTx->isRunning();
        s_status.chargePermitted = owner_session_matches();
        if (connected && !wasConnected) {
            if (connectedOnce) ++s_status.reconnectCount;
            connectedOnce = true;
        }
        s_status.lastMessageTimeMs = initialized ? connection->getLastRecv() : 0;
        s_status.connectionState = !config.enabled ? ocpp_connection_state_t::Disabled :
            initFailed ? ocpp_connection_state_t::Faulted :
            !online ? ocpp_connection_state_t::WaitingForNetwork :
            waitingForTime ? ocpp_connection_state_t::WaitingForTime :
            !connected ? ocpp_connection_state_t::Connecting :
            connection && connection->bootAccepted ? ocpp_connection_state_t::Ready :
            connection && connection->bootRejected ? ocpp_connection_state_t::BootRejected :
            ocpp_connection_state_t::BootPending;
        portEXIT_CRITICAL(&s_stateMux);
        if (connected && !wasConnected) {
            set_last_error("");
            log_event(tls ? "WebSocket connected; TLS certificate verified" : "WebSocket connected (unencrypted)");
        } else if (!connected && wasConnected) {
            // Require a fresh announcement after reconnect, even if CP did not change.
            ++statusRevision;
            lastStatusAttemptMs = millis() - 5000;
            log_event("WebSocket disconnected");
        }
        wasConnected = connected;
        if (nowMs - lastSummaryMs >= 30000) {
            lastSummaryMs = nowMs;
            ESP_LOGI(TAG, "OCPP: network=%d ws=%d boot=%d heap=%u stackFree=%u",
                     online, connected, connection && connection->bootAccepted, ESP.getFreeHeap(),
                     unsigned(uxTaskGetStackHighWaterMark(nullptr)));
        }
        ulTaskNotifyTake(pdTRUE, online ? OCPP_TASK_IDLE_TICKS : pdMS_TO_TICKS(500));
    }
}

bool ocpp_start() {
    ocpp_config_t config{};
    config.enabled = false;
    config.securityProfile = ocpp_security_profile_t::UnsecuredDevelopment;
    config.allowUnsecuredDevelopment = true;
    copy_text(config.chargePointId, sizeof(config.chargePointId), DEFAULT_CHARGE_POINT_ID);
    copy_text(config.chargePointVendor, sizeof(config.chargePointVendor), "InnoCharge");
    copy_text(config.chargePointModel, sizeof(config.chargePointModel), "AdvancedCharger");
    Preferences store;
    if (store.begin("ocpp", false)) {
        const size_t size = store.getBytesLength("config");
        std::unique_ptr<uint8_t[]> bytes;
        if (size >= sizeof(StoredConfig) && size <= sizeof(StoredConfig) + CA_PEM_MAX + 1)
            bytes.reset(new (std::nothrow) uint8_t[size]);
        bool valid = false;
        if (bytes && store.getBytes("config", bytes.get(), size) == size) {
            StoredConfig saved{};
            memcpy(&saved, bytes.get(), sizeof(saved));
            const bool legacy = saved.magic == CONFIG_MAGIC_V1 && size == sizeof(saved);
            const char* pem = legacy ? "" : reinterpret_cast<const char*>(bytes.get() + sizeof(saved));
            const size_t pemSize = size - sizeof(saved);
            valid = legacy || (saved.magic == CONFIG_MAGIC && pemSize &&
                memchr(pem, '\0', pemSize) == pem + pemSize - 1);
            String caError;
            if (valid && *pem) valid = validate_ca(pem, caError);
            const char* error = nullptr;
            ocpp_config_t check = saved.config;
            check.enabled = true;
            String host, path;
            uint16_t port;
            valid = valid && validate_config(check, &error) &&
                parse_test_endpoint(check.backendUrl, host, port, path) &&
                (check.securityProfile != ocpp_security_profile_t::TlsBasicAuthentication || *pem);
            if (valid) {
                config = saved.config;
                copy_text(s_caPem, sizeof(s_caPem), pem);
            }
            mbedtls_platform_zeroize(&saved, sizeof(saved));
            mbedtls_platform_zeroize(bytes.get(), size);
        }
        if (size && !valid) log_event("Stored OCPP settings invalid; starting disabled with defaults");
        store.end();
    } else log_event("OCPP settings storage unavailable; starting disabled");
#ifdef FW_VERSION_MAIN
    copy_text(config.firmwareVersion, sizeof(config.firmwareVersion), FW_VERSION_MAIN);
#else
    copy_text(config.firmwareVersion, sizeof(config.firmwareVersion), "unknown");
#endif
    if (!ocpp_set_config(config)) return false;
    if (xTaskCreatePinnedToCore(A_Task_OCPP, "Task_OCPP", OCPP_TASK_STACK_BYTES,
                                nullptr, 1, nullptr, 1) != pdPASS) {
        set_last_error("Unable to create OCPP task");
        ESP_LOGE(TAG, "Unable to create OCPP task");
        return false;
    }
    return true;
}

// ================= WEB CONFIGURATION / READ-ONLY DIAGNOSTICS =================
// These functions never call MicroOCPP. Changes wake its sole owner task.
bool ocpp_ui_save(JsonObjectConst settings, bool enabledOnly, String& error) {
    ocpp_config_t config{};
    std::unique_ptr<char[]> ca(new (std::nothrow) char[CA_PEM_MAX + 1]);
    if (!ca) { error = "Insufficient memory for settings."; return false; }
    portENTER_CRITICAL(&s_stateMux);
    config = s_config;
    memcpy(ca.get(), s_caPem, CA_PEM_MAX + 1);
    const bool ready = s_taskHandle != nullptr && s_status.taskInitialized;
    portEXIT_CRITICAL(&s_stateMux);
    if (!ready) { error = "OCPP controller unavailable (startup or rescue mode)."; return false; }
    if (enabledOnly) {
        if (!settings["enabled"].is<bool>()) { error = "Enable must be true or false."; return false; }
        config.enabled = settings["enabled"].as<bool>();
    } else {
        if (!settings["securityProfile"].is<int>() ||
            (settings["securityProfile"].as<int>() != 0 && settings["securityProfile"].as<int>() != 2)) {
            error = "Select Security Profile 0 or 2. Reload the page after a UI update.";
            return false;
        }
        config.securityProfile = static_cast<ocpp_security_profile_t>(settings["securityProfile"].as<int>());
        config.allowUnsecuredDevelopment = config.securityProfile == ocpp_security_profile_t::UnsecuredDevelopment;
        if (!settings["authorizationKey"].is<const char*>()) {
            error = "Missing password field (leave empty to keep saved password).";
            return false;
        }
        const JsonString key = settings["authorizationKey"].as<JsonString>();
        if (key.size() != strlen(key.c_str()) || key.size() >= sizeof(config.authorizationKey)) {
            error = "Invalid password length or embedded NUL."; return false;
        }
        if (key.size()) {
            if (!ocpp_auth::valid_password(key.c_str())) {
                error = "Password: 16-20 printable ASCII characters, or hex: followed by 32-40 hex digits.";
                return false;
            }
            copy_text(config.authorizationKey, sizeof(config.authorizationKey), key.c_str());
        }
        if (!settings["caPem"].isNull()) {
            if (!settings["caPem"].is<const char*>()) { error = "CA must be PEM text."; return false; }
            const JsonString pem = settings["caPem"].as<JsonString>();
            if (pem.size() != strlen(pem.c_str()) || pem.size() > CA_PEM_MAX || !validate_ca(pem.c_str(), error)) {
                if (error.isEmpty()) error = "CA PEM exceeds 4096 bytes or contains NUL.";
                return false;
            }
            copy_text(ca.get(), CA_PEM_MAX + 1, pem.c_str());
            copy_text(config.caCertificatePath, sizeof(config.caCertificatePath), CA_STORAGE_ID);
        }
        struct Field { const char* key; char* value; size_t size; };
        Field fields[] = {
            {"backendUrl", config.backendUrl, sizeof(config.backendUrl)},
            {"chargePointId", config.chargePointId, sizeof(config.chargePointId)},
            {"vendor", config.chargePointVendor, sizeof(config.chargePointVendor)},
            {"model", config.chargePointModel, sizeof(config.chargePointModel)},
            {"serialNumber", config.chargePointSerialNumber, sizeof(config.chargePointSerialNumber)}
        };
        for (const auto& field : fields) {
            if (!settings[field.key].is<const char*>()) {
                error = String("Missing text field: ") + field.key;
                return false;
            }
            const char* value = settings[field.key].as<const char*>();
            if (strlen(value) >= field.size || settings[field.key].as<JsonString>().size() != strlen(value)) {
                error = String("Field too long: ") + field.key;
                return false;
            }
            copy_text(field.value, field.size, value);
        }
    }
    // Validate editable settings even when currently disabled. Never silently
    // truncate or save a URL that the transport cannot actually use.
    ocpp_config_t check = config;
    check.enabled = true;
    const char* validationError = nullptr;
    if (!validate_config(check, &validationError)) { error = validationError; return false; }
    String host, path;
    uint16_t port;
    if (!parse_test_endpoint(config.backendUrl, host, port, path)) {
        error = "Use ws:// (Profile 0) or wss:// (Profile 2), without credentials, query or fragment.";
        return false;
    }
    if (check.securityProfile == ocpp_security_profile_t::TlsBasicAuthentication && !ca[0]) {
        error = "Import a CA certificate before selecting Profile 2."; return false;
    }
    // Do not reconnect or write flash again for an unchanged request.
    portENTER_CRITICAL(&s_stateMux);
    const bool unchanged = memcmp(&config, &s_config, sizeof(config)) == 0 && strcmp(ca.get(), s_caPem) == 0;
    portEXIT_CRITICAL(&s_stateMux);
    if (unchanged) return true;
    // Reserve the configuration change before writing NVS. An authorization
    // cannot begin between this check and applying the new socket settings.
    portENTER_CRITICAL(&s_stateMux);
    const bool busy = s_authorizationBusy || s_configSaving || s_resetPending;
    if (!busy) s_configSaving = true;
    portEXIT_CRITICAL(&s_stateMux);
    if (busy) { error = "End OCPP session and wait for StopTransaction first."; return false; }
    struct ConfigSaveGuard {
        ~ConfigSaveGuard() {
            portENTER_CRITICAL(&s_stateMux);
            s_configSaving = false;
            portEXIT_CRITICAL(&s_stateMux);
        }
    } configSaveGuard;
    StoredConfig saved{};
    saved.magic = CONFIG_MAGIC;
    saved.config = config;
    const size_t size = sizeof(saved) + strlen(ca.get()) + 1;
    std::unique_ptr<uint8_t[]> bytes(new (std::nothrow) uint8_t[size]);
    if (!bytes) { error = "Insufficient memory for settings storage."; return false; }
    memcpy(bytes.get(), &saved, sizeof(saved));
    memcpy(bytes.get() + sizeof(saved), ca.get(), size - sizeof(saved));
    mbedtls_platform_zeroize(&saved, sizeof(saved));
    Preferences store;
    if (!store.begin("ocpp", false)) {
        mbedtls_platform_zeroize(bytes.get(), size);
        error = "Cannot open OCPP settings storage.";
        return false;
    }
    const bool persisted = store.putBytes("config", bytes.get(), size) == size;
    mbedtls_platform_zeroize(bytes.get(), size);
    store.end();
    if (!persisted) { error = "Cannot save OCPP settings; configuration unchanged."; return false; }
    if (!apply_config(config, ca.get())) { error = "OCPP configuration rejected."; return false; }
    log_event(enabledOnly ? "Enable setting saved" : "Connection settings saved");
    return true;
}

void ocpp_ui_clear_log() {
    portENTER_CRITICAL(&s_stateMux);
    s_logNext = 0;
    s_logCount = 0;
    ++s_logRevision;
    portEXIT_CRITICAL(&s_stateMux);
}

String ocpp_ui_state(bool includeLog) {
    ocpp_config_t config{};
    ocpp_runtime_status_t status{};
    bool pending, controllerReady, caConfigured;
    uint32_t logRevision;
    portENTER_CRITICAL(&s_stateMux);
    config = s_config;
    caConfigured = s_caPem[0] != '\0';
    status = s_status;
    pending = s_configRevision != s_appliedRevision;
    controllerReady = s_taskHandle != nullptr && s_status.taskInitialized;
    logRevision = s_logRevision;
    portEXIT_CRITICAL(&s_stateMux);
    JsonDocument doc;
    doc["type"] = "ocppState";
    JsonObject cfg = doc["config"].to<JsonObject>();
    cfg["enabled"] = config.enabled;
    cfg["backendUrl"] = config.backendUrl;
    cfg["chargePointId"] = config.chargePointId;
    cfg["vendor"] = config.chargePointVendor;
    cfg["model"] = config.chargePointModel;
    cfg["serialNumber"] = config.chargePointSerialNumber;
    cfg["firmware"] = config.firmwareVersion;
    cfg["securityProfile"] = static_cast<int>(config.securityProfile);
    cfg["passwordSet"] = config.authorizationKey[0] != '\0';
    cfg["caConfigured"] = caConfigured;
    // No authorization key / CA content / backend payload is exposed.
    JsonObject state = doc["status"].to<JsonObject>();
    const char* label = !controllerReady ? "Controller unavailable" :
        pending ? "Applying settings" :
        !config.enabled ? "Disabled (controller sleeping)" :
        status.connectionState == ocpp_connection_state_t::Faulted ? "Initialization failed" :
        !status.networkReady ? "Waiting for network" :
        status.connectionState == ocpp_connection_state_t::WaitingForTime ? "Waiting for device time (NTP)" :
        !status.websocketConnected ? "Connecting" :
        status.bootAccepted && !status.backendOperative ? "OCPP unavailable / local RFID rules unchanged" :
        status.bootAccepted ? "Connected / Boot accepted" :
        status.connectionState == ocpp_connection_state_t::BootRejected ? "Boot rejected" : "Waiting for Boot acceptance";
    state["label"] = label;
    state["controllerReady"] = controllerReady;
    state["pending"] = pending;
    state["stackInitialized"] = status.stackInitialized;
    state["connected"] = status.websocketConnected;
    state["tlsVerified"] = !pending && config.enabled && status.tlsVerified &&
        config.securityProfile == ocpp_security_profile_t::TlsBasicAuthentication;
    state["timeValid"] = time_service_is_valid();
    state["securityProfile"] = static_cast<int>(config.securityProfile);
    state["bootAccepted"] = status.bootAccepted;
    state["backendOperative"] = status.backendOperative;
    state["heartbeatIntervalSeconds"] = status.heartbeatIntervalSeconds;
    state["meterIntervalSeconds"] = status.meterIntervalSeconds;
    state["heartbeats"] = status.heartbeatConfirmations;
    state["heartbeatAgeSeconds"] = status.heartbeatConfirmations ?
        int32_t((millis() - status.lastHeartbeatTimeMs) / 1000) : -1;
    state["reconnects"] = status.reconnectCount;
    state["freeHeap"] = ESP.getFreeHeap();
    state["lastError"] = status.lastError;
    state["logRevision"] = logRevision;
    if (includeLog) {
        std::unique_ptr<LogEntry[]> snapshot(new (std::nothrow) LogEntry[LOG_CAPACITY]);
        if (snapshot) {
            size_t count, next;
            portENTER_CRITICAL(&s_stateMux);
            count = s_logCount;
            next = s_logNext;
            memcpy(snapshot.get(), s_log, sizeof(s_log));
            logRevision = s_logRevision;
            portEXIT_CRITICAL(&s_stateMux);
            state["logRevision"] = logRevision;
            JsonArray entries = doc["log"].to<JsonArray>();
            for (size_t i = 0; i < count; ++i) {
                const LogEntry& entry = snapshot[(next + LOG_CAPACITY - count + i) % LOG_CAPACITY];
                JsonObject item = entries.add<JsonObject>();
                item["atMs"] = entry.atMs;
                // ArduinoJson 7.2 links const char* by default. The snapshot
                // is freed before serializeJson below, so JSON must own a copy.
                item["message"] = JsonString(entry.text, JsonString::Copied);
            }
        }
    }
    String out;
    serializeJson(doc, out);
    return out;
}
