#include "session_mailer.hpp"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "charge_session_log.hpp"

extern Preferences preferences;

static constexpr const char* KEY_SERVER = "mailServer";
static constexpr const char* KEY_PORT = "mailPort";
static constexpr const char* KEY_SSL = "mailSsl";
static constexpr const char* KEY_SECURITY = "mailSecurity";
static constexpr const char* KEY_USER = "mailUser";
static constexpr const char* KEY_PASS = "mailPass";
static constexpr const char* KEY_FROM = "mailFrom";
static constexpr const char* KEY_TO = "mailTo";
static constexpr const char* KEY_SUBJECT = "mailSubject";
static constexpr const char* KEY_ENABLE = "mailEnable";
static constexpr const char* KEY_MODE = "mailMode";
static constexpr const char* KEY_LAST_EACH_TX = "mailEachTx";
static constexpr const char* KEY_EACH_INITIALIZED = "mailEachInit";
static constexpr const char* KEY_LAST_DAILY = "mailLastDay";
static constexpr const char* KEY_LAST_WEEKLY = "mailLastWeek";
static constexpr const char* KEY_LAST_MONTHLY = "mailLastMonth";
static constexpr const char* MAIL_LOG_PATH = "/mailer_log.txt";
static constexpr uint8_t MAIL_LOG_MAX_ENTRIES = 30;

static String s_lastStatus = "";
static String s_mailLog = "";
static String s_lastMailFailureSignature = "";
static uint32_t s_lastMailFailureLogMillis = 0;
static SemaphoreHandle_t s_mailLogMutex = nullptr;
static volatile bool s_mailerBusy = false;

enum class mail_security_t : uint8_t {
    none = 0,
    ssl = 1,
    starttls = 2
};

static String loadMailerSessionsJson()
{
    return charge_session_log_to_json_page(0, 100);
}

static void trimMailLog()
{
    uint8_t lines = 0;
    for (size_t i = 0; i < s_mailLog.length(); ++i) {
        if (s_mailLog.charAt(i) == '\n') ++lines;
    }
    while (lines > MAIL_LOG_MAX_ENTRIES) {
        const int firstLineEnd = s_mailLog.indexOf('\n');
        if (firstLineEnd < 0) break;
        s_mailLog.remove(0, firstLineEnd + 1);
        --lines;
    }
}

static String mailLogTimestamp()
{
    const time_t now = time(nullptr);
    if (now >= 1700000000) {
        tm localTime {};
        localtime_r(&now, &localTime);
        char timestamp[24] {};
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &localTime);
        return String(timestamp);
    }
    return "Uptime " + String(millis() / 1000UL) + " s";
}

static uint8_t mailRecipientCount(const String& recipients)
{
    uint8_t count = 0;
    int start = 0;
    while (start < (int)recipients.length()) {
        const int comma = recipients.indexOf(',', start);
        String recipient = comma >= 0 ? recipients.substring(start, comma) : recipients.substring(start);
        recipient.trim();
        if (recipient.length() > 0 && count < UINT8_MAX) ++count;
        if (comma < 0) break;
        start = comma + 1;
    }
    return count;
}

static void appendMailLog(
    bool success,
    const String& reportLabel,
    size_t sessionCount,
    const String& recipients,
    const String& detail)
{
    if (!success) {
        const String failureSignature = reportLabel + "|" + detail;
        if (failureSignature == s_lastMailFailureSignature &&
            millis() - s_lastMailFailureLogMillis < 15UL * 60UL * 1000UL) {
            return;
        }
        s_lastMailFailureSignature = failureSignature;
        s_lastMailFailureLogMillis = millis();
    } else {
        s_lastMailFailureSignature = "";
        s_lastMailFailureLogMillis = 0;
    }

    String line = mailLogTimestamp();
    line += success ? " | SENT" : " | FAILED";
    line += " | " + reportLabel;
    line += " | " + String(sessionCount) + (sessionCount == 1 ? " session" : " sessions");
    line += " | " + String(mailRecipientCount(recipients)) + " recipient(s)";
    if (detail.length() > 0) line += " | " + detail;
    line += "\n";

    if (s_mailLogMutex) xSemaphoreTake(s_mailLogMutex, portMAX_DELAY);
    s_mailLog += line;
    trimMailLog();
    File file = SPIFFS.open(MAIL_LOG_PATH, FILE_WRITE);
    if (file) {
        file.print(s_mailLog);
        file.close();
    }
    if (s_mailLogMutex) xSemaphoreGive(s_mailLogMutex);
}

static String mailLogSnapshot()
{
    if (s_mailLogMutex) xSemaphoreTake(s_mailLogMutex, portMAX_DELAY);
    String snapshot = s_mailLog;
    if (s_mailLogMutex) xSemaphoreGive(s_mailLogMutex);
    return snapshot;
}

struct mail_settings_t {
    String server;
    uint16_t port = 587;
    mail_security_t security = mail_security_t::starttls;
    String username;
    String password;
    String from;
    String to;
    String subject;
    bool enable = false;
    uint8_t mode = 0; // 0=off, 1=after each, 2=daily, 3=weekly, 4=monthly
};

static String readPrefString(const char* key, const char* fallback = "")
{
    return preferences.getString(key, fallback);
}

static String subjectWithWallboxName(const String& subject)
{
    String wallboxName = readPrefString("wallboxName", "InnoCharge");
    wallboxName.trim();
    if (wallboxName.length() == 0) wallboxName = "InnoCharge";
    return subject + " - " + wallboxName;
}

static mail_settings_t loadSettings()
{
    mail_settings_t settings;
    settings.server = readPrefString(KEY_SERVER, "smtp.world4you.com");
    if (preferences.isKey(KEY_SECURITY)) {
        settings.security = static_cast<mail_security_t>(
            min<uint8_t>(preferences.getUChar(KEY_SECURITY, 2), 2));
    } else if (preferences.isKey(KEY_SSL)) {
        settings.security = preferences.getBool(KEY_SSL, true)
            ? mail_security_t::ssl
            : mail_security_t::none;
    }
    const uint16_t defaultPort = settings.security == mail_security_t::starttls ? 587 : 465;
    settings.port = preferences.getUShort(KEY_PORT, defaultPort);
    settings.username = readPrefString(KEY_USER, "report@innocharge.at");
    settings.password = readPrefString(KEY_PASS);
    settings.from = readPrefString(KEY_FROM, "report@innocharge.at");
    settings.to = readPrefString(KEY_TO);
    settings.subject = readPrefString(KEY_SUBJECT, "InnoCharge charge sessions");
    settings.enable = preferences.getBool(KEY_ENABLE, false);
    settings.mode = preferences.getUChar(KEY_MODE, 0);
    return settings;
}

static const char* securityName(mail_security_t security)
{
    switch (security) {
        case mail_security_t::none: return "none";
        case mail_security_t::ssl: return "ssl";
        case mail_security_t::starttls: return "starttls";
    }
    return "starttls";
}

class StartTlsClient : public WiFiClientSecure {
public:
    int connect(IPAddress ip, uint16_t port) override
    {
        stop();
        ssl_init(sslclient);
        sslclient->socket = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sslclient->socket < 0) return 0;

        const int flags = fcntl(sslclient->socket, F_GETFL, 0);
        if (flags >= 0) fcntl(sslclient->socket, F_SETFL, flags | O_NONBLOCK);

        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = ip;
        address.sin_port = htons(port);

        int result = lwip_connect(
            sslclient->socket,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address));
        if (result < 0 && errno != EINPROGRESS) {
            stop();
            return 0;
        }

        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(sslclient->socket, &writeSet);
        timeval timeout {10, 0};
        result = select(sslclient->socket + 1, nullptr, &writeSet, nullptr, &timeout);
        if (result <= 0) {
            stop();
            return 0;
        }

        int socketError = 0;
        socklen_t errorLength = sizeof(socketError);
        if (getsockopt(sslclient->socket, SOL_SOCKET, SO_ERROR, &socketError, &errorLength) < 0 ||
            socketError != 0) {
            stop();
            return 0;
        }

        timeval ioTimeout {10, 0};
        lwip_setsockopt(sslclient->socket, SOL_SOCKET, SO_RCVTIMEO, &ioTimeout, sizeof(ioTimeout));
        lwip_setsockopt(sslclient->socket, SOL_SOCKET, SO_SNDTIMEO, &ioTimeout, sizeof(ioTimeout));
        sslclient->socket_timeout = 10000;
        sslclient->handshake_timeout = 30000;
        _connected = true;
        tlsActive = false;
        return 1;
    }

    int connect(const char* host, uint16_t port) override
    {
        IPAddress ip;
        if (!WiFi.hostByName(host, ip)) return 0;
        return connect(ip, port);
    }

    bool startTls(const char* hostname)
    {
        static const unsigned char personalisation[] = "innocharge-smtp";
        mbedtls_entropy_init(&sslclient->entropy_ctx);

        int result = mbedtls_ctr_drbg_seed(
            &sslclient->drbg_ctx,
            mbedtls_entropy_func,
            &sslclient->entropy_ctx,
            personalisation,
            sizeof(personalisation) - 1);
        if (result != 0) return false;

        result = mbedtls_ssl_config_defaults(
            &sslclient->ssl_conf,
            MBEDTLS_SSL_IS_CLIENT,
            MBEDTLS_SSL_TRANSPORT_STREAM,
            MBEDTLS_SSL_PRESET_DEFAULT);
        if (result != 0) return false;

        // Matches the existing implicit-TLS mailer behaviour. The connection is
        // encrypted, while certificate pinning can be added independently later.
        mbedtls_ssl_conf_authmode(&sslclient->ssl_conf, MBEDTLS_SSL_VERIFY_NONE);
        mbedtls_ssl_conf_rng(
            &sslclient->ssl_conf,
            mbedtls_ctr_drbg_random,
            &sslclient->drbg_ctx);

        result = mbedtls_ssl_setup(&sslclient->ssl_ctx, &sslclient->ssl_conf);
        if (result != 0) return false;
        result = mbedtls_ssl_set_hostname(&sslclient->ssl_ctx, hostname);
        if (result != 0) return false;

        mbedtls_ssl_set_bio(
            &sslclient->ssl_ctx,
            &sslclient->socket,
            mbedtls_net_send,
            mbedtls_net_recv,
            nullptr);

        const uint32_t deadline = millis() + sslclient->handshake_timeout;
        do {
            result = mbedtls_ssl_handshake(&sslclient->ssl_ctx);
            if (result == 0) {
                tlsActive = true;
                return true;
            }
            if (result != MBEDTLS_ERR_SSL_WANT_READ &&
                result != MBEDTLS_ERR_SSL_WANT_WRITE) {
                return false;
            }
            vTaskDelay(2);
        } while ((int32_t)(deadline - millis()) > 0);

        return false;
    }

    size_t write(uint8_t value) override
    {
        return write(&value, 1);
    }

    size_t write(const uint8_t* buffer, size_t size) override
    {
        if (!_connected || sslclient->socket < 0) return 0;
        size_t totalSent = 0;
        const uint32_t deadline = millis() + 10000UL;

        while (totalSent < size && (int32_t)(deadline - millis()) > 0) {
            const int sent = tlsActive
                ? send_ssl_data(sslclient, buffer + totalSent, size - totalSent)
                : lwip_send(sslclient->socket, buffer + totalSent, size - totalSent, 0);
            if (sent > 0) {
                totalSent += static_cast<size_t>(sent);
                continue;
            }
            if (!tlsActive && sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                _connected = false;
                break;
            }
            vTaskDelay(1);
        }
        return totalSent;
    }

    int available() override
    {
        if (!_connected || sslclient->socket < 0) return 0;
        if (tlsActive) return max(0, data_to_read(sslclient));
        int count = 0;
        return lwip_ioctl(sslclient->socket, FIONREAD, &count) == 0 ? count : 0;
    }

    int read() override
    {
        uint8_t value = 0;
        return read(&value, 1) == 1 ? value : -1;
    }

    int read(uint8_t* buffer, size_t size) override
    {
        if (!_connected || sslclient->socket < 0 || size == 0) return -1;
        const int received = tlsActive
            ? get_ssl_receive(sslclient, buffer, size)
            : lwip_recv(sslclient->socket, buffer, size, 0);
        if (received == 0) _connected = false;
        if (!tlsActive && received < 0 && errno != EAGAIN && errno != EWOULDBLOCK) _connected = false;
        return received;
    }

    int peek() override
    {
        if (tlsActive) return WiFiClientSecure::peek();
        uint8_t value = 0;
        const int received = lwip_recv(sslclient->socket, &value, 1, MSG_PEEK);
        return received == 1 ? value : -1;
    }

    void flush() override {}

    void stop() override
    {
        tlsActive = false;
        WiFiClientSecure::stop();
    }

    uint8_t connected() override
    {
        return _connected && sslclient->socket >= 0;
    }

    operator bool() override
    {
        return connected();
    }

private:
    bool tlsActive = false;
};

static String b64(const String& input)
{
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String out;
    int val = 0;
    int valb = -6;

    for (size_t i = 0; i < input.length(); ++i) {
        val = (val << 8) + (uint8_t)input[i];
        valb += 8;
        while (valb >= 0) {
            out += alphabet[(val >> valb) & 0x3F];
            valb -= 6;
        }
    }
    if (valb > -6) out += alphabet[((val << 8) >> (valb + 8)) & 0x3F];
    while (out.length() % 4) out += '=';
    return out;
}

static String b64Wrapped(const String& input)
{
    String encoded = b64(input);
    String wrapped;
    for (size_t i = 0; i < encoded.length(); i += 76) {
        wrapped += encoded.substring(i, i + 76);
        wrapped += "\r\n";
    }
    return wrapped;
}

static bool readSmtpResponse(Client& client, int expectedCode)
{
    uint32_t deadline = millis() + 10000UL;
    int lastCode = 0;

    while (millis() < deadline) {
        String line = client.readStringUntil('\n');
        if (line.length() == 0) continue;
        line.trim();
        if (line.length() < 3) continue;

        lastCode = line.substring(0, 3).toInt();
        if (line.length() >= 4 && line.charAt(3) == ' ') {
            return lastCode == expectedCode;
        }
    }

    return false;
}

static bool sendCommand(Client& client, const String& command, int expectedCode)
{
    client.print(command);
    client.print("\r\n");
    return readSmtpResponse(client, expectedCode);
}

static String csvEscape(const String& value)
{
    String out = "\"";
    for (size_t i = 0; i < value.length(); ++i) {
        if (value.charAt(i) == '"') out += "\"\"";
        else out += value.charAt(i);
    }
    out += "\"";
    return out;
}

static bool includeSession(JsonObject session, uint32_t sinceTx, time_t sinceEpoch, time_t untilEpoch)
{
    if (session["active"] | false) return false;
    if (sinceTx > 0 && (uint32_t)(session["transactionId"] | 0) <= sinceTx) return false;

    time_t stopTime = (time_t)(session["stopTimeEpoch"] | 0);
    if (sinceEpoch > 0 && stopTime <= sinceEpoch) return false;
    if (untilEpoch > 0 && stopTime > untilEpoch) return false;
    return true;
}

static JsonDocument filteredSessions(JsonArray sessions, uint32_t sinceTx, time_t sinceEpoch, time_t untilEpoch, uint32_t* maxTx, time_t* maxStopTime)
{
    JsonDocument filtered;
    JsonArray out = filtered["sessions"].to<JsonArray>();
    if (maxTx) *maxTx = 0;
    if (maxStopTime) *maxStopTime = 0;

    for (JsonObject session : sessions) {
        if (!includeSession(session, sinceTx, sinceEpoch, untilEpoch)) continue;
        JsonObject copy = out.add<JsonObject>();
        copy.set(session);

        uint32_t tx = session["transactionId"] | 0;
        time_t stopTime = (time_t)(session["stopTimeEpoch"] | 0);
        if (maxTx && tx > *maxTx) *maxTx = tx;
        if (maxStopTime && stopTime > *maxStopTime) *maxStopTime = stopTime;
    }
    return filtered;
}

static String buildCsv(JsonArray sessions)
{
    String csv = "Transaction,Status,ID Tag,User,Start,Stop,Duration Seconds,Energy Wh,Stop Reason\r\n";
    for (JsonObject session : sessions) {
        csv += String((uint32_t)(session["transactionId"] | 0)) + ",";
        csv += csvEscape((session["active"] | false) ? "active" : "closed") + ",";
        csv += csvEscape(session["idTag"] | "") + ",";
        csv += csvEscape(session["userName"] | "") + ",";
        csv += csvEscape(session["startTime"] | "") + ",";
        csv += csvEscape(session["stopTime"] | "") + ",";
        csv += String((uint32_t)(session["durationSeconds"] | 0)) + ",";
        csv += String((int32_t)(session["energyWh"] | 0)) + ",";
        csv += csvEscape(session["stopReason"] | "");
        csv += "\r\n";
    }
    return csv;
}

static String buildBody(JsonArray sessions, const String& reportLabel)
{
    String body = "InnoCharge charge session report";
    if (reportLabel.length() > 0) {
        body += " - " + reportLabel;
    }
    body += "\r\n\r\n";
    uint16_t count = 0;
    float totalWh = 0.0f;

    for (JsonObject session : sessions) {
        if (session["active"] | false) continue;
        ++count;
        totalWh += session["energyWh"] | 0.0f;
    }

    body += "Closed sessions: " + String(count) + "\r\n";
    body += "Total energy: " + String(totalWh / 1000.0f, 3) + " kWh\r\n\r\n";

    for (JsonObject session : sessions) {
        body += "#";
        body += String((uint32_t)(session["transactionId"] | 0));
        body += " | ";
        body += (const char*)(session["startTime"] | "");
        body += " - ";
        body += (const char*)(session["stopTime"] | "");
        body += " | ";
        body += String((float)(session["energyWh"] | 0.0f) / 1000.0f, 3);
        body += " kWh | ";
        body += (const char*)(session["idTag"] | "");
        body += " | ";
        body += (const char*)(session["userName"] | "");
        body += " | ";
        body += (const char*)(session["stopReason"] | "");
        body += "\r\n";
    }

    return body;
}

static String firstRecipient(const String& recipients)
{
    int comma = recipients.indexOf(',');
    String out = comma >= 0 ? recipients.substring(0, comma) : recipients;
    out.trim();
    return out;
}

static bool sendRecipients(Client& client, const String& recipients)
{
    int start = 0;
    while (start < (int)recipients.length()) {
        int comma = recipients.indexOf(',', start);
        String recipient = comma >= 0 ? recipients.substring(start, comma) : recipients.substring(start);
        recipient.trim();
        if (recipient.length() > 0 && !sendCommand(client, "RCPT TO:<" + recipient + ">", 250)) return false;
        if (comma < 0) break;
        start = comma + 1;
    }
    return true;
}

static bool sendReportPayload(Client& client, const mail_settings_t& settings, const String& body, const String& csv)
{
    if (settings.username.length() > 0) {
        if (!sendCommand(client, "AUTH LOGIN", 334)) return false;
        if (!sendCommand(client, b64(settings.username), 334)) return false;
        if (!sendCommand(client, b64(settings.password), 235)) return false;
    }

    if (!sendCommand(client, "MAIL FROM:<" + settings.from + ">", 250)) return false;
    if (!sendRecipients(client, settings.to)) return false;
    if (!sendCommand(client, "DATA", 354)) return false;

    const String boundary = "----InnoChargeSessionReport";
    client.print("From: <" + settings.from + ">\r\n");
    client.print("To: <" + firstRecipient(settings.to) + ">\r\n");
    client.print("Subject: " + subjectWithWallboxName(settings.subject) + "\r\n");
    client.print("MIME-Version: 1.0\r\n");
    client.print("Content-Type: multipart/mixed; boundary=\"" + boundary + "\"\r\n\r\n");
    client.print("--" + boundary + "\r\n");
    client.print("Content-Type: text/plain; charset=utf-8\r\n\r\n");
    client.print(body);
    client.print("\r\n--" + boundary + "\r\n");
    client.print("Content-Type: text/csv; name=\"charge_sessions.csv\"\r\n");
    client.print("Content-Disposition: attachment; filename=\"charge_sessions.csv\"\r\n");
    client.print("Content-Transfer-Encoding: base64\r\n\r\n");
    client.print(b64Wrapped(csv));
    client.print("--" + boundary + "--\r\n.\r\n");

    if (!readSmtpResponse(client, 250)) return false;
    sendCommand(client, "QUIT", 221);
    return true;
}

static bool sendReportWithClient(Client& client, const mail_settings_t& settings, const String& body, const String& csv)
{
    client.setTimeout(10000);
    if (!readSmtpResponse(client, 220)) {
        s_lastStatus = "SMTP greeting failed";
        return false;
    }
    if (!sendCommand(client, "EHLO innocharge.local", 250)) {
        s_lastStatus = "SMTP EHLO failed";
        return false;
    }
    return sendReportPayload(client, settings, body, csv);
}

static bool sendReportWithStartTls(StartTlsClient& client, const mail_settings_t& settings, const String& body, const String& csv)
{
    client.setTimeout(10000);
    if (!readSmtpResponse(client, 220)) {
        s_lastStatus = "SMTP greeting failed";
        return false;
    }
    if (!sendCommand(client, "EHLO innocharge.local", 250)) {
        s_lastStatus = "SMTP EHLO failed";
        return false;
    }
    if (!sendCommand(client, "STARTTLS", 220)) {
        s_lastStatus = "SMTP STARTTLS rejected";
        return false;
    }
    if (!client.startTls(settings.server.c_str())) {
        s_lastStatus = "TLS handshake failed";
        return false;
    }
    if (!sendCommand(client, "EHLO innocharge.local", 250)) {
        s_lastStatus = "SMTP EHLO after TLS failed";
        return false;
    }
    return sendReportPayload(client, settings, body, csv);
}

static bool resolveServer(const String& host, IPAddress& ip)
{
    if (ip.fromString(host)) {
        return true;
    }

    struct addrinfo hints {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* result = nullptr;
    int rc = getaddrinfo(host.c_str(), nullptr, &hints, &result);
    if (rc != 0 || result == nullptr) {
        if (result) freeaddrinfo(result);
        return false;
    }

    sockaddr_in* addr = reinterpret_cast<sockaddr_in*>(result->ai_addr);
    ip = IPAddress(addr->sin_addr.s_addr);
    freeaddrinfo(result);
    return true;
}

void session_mailer_begin()
{
    s_lastStatus = "";
    if (!s_mailLogMutex) s_mailLogMutex = xSemaphoreCreateMutex();
    if (s_mailLogMutex) xSemaphoreTake(s_mailLogMutex, portMAX_DELAY);
    File logFile = SPIFFS.open(MAIL_LOG_PATH, FILE_READ);
    s_mailLog = logFile ? logFile.readString() : "";
    if (logFile) logFile.close();
    trimMailLog();
    if (s_mailLogMutex) xSemaphoreGive(s_mailLogMutex);

    xTaskCreatePinnedToCore([](void*) {
        for (;;) {
            session_mailer_run_automatic();
            vTaskDelay(pdMS_TO_TICKS(60000));
        }
    }, "SessionMailer", 8192, nullptr, 1, nullptr, 1);
}

void session_mailer_append_status(JsonObject root)
{
    mail_settings_t settings = loadSettings();
    JsonObject mailer = root["mailer"].to<JsonObject>();
    mailer["server"] = settings.server;
    mailer["port"] = settings.port;
    mailer["security"] = securityName(settings.security);
    mailer["ssl"] = settings.security == mail_security_t::ssl;
    mailer["username"] = settings.username;
    mailer["from"] = settings.from;
    mailer["to"] = settings.to;
    mailer["subject"] = settings.subject;
    mailer["enable"] = settings.enable;
    mailer["mode"] = settings.mode;
    mailer["passwordSet"] = settings.password.length() > 0;
    mailer["lastStatus"] = s_lastStatus;
    mailer["log"] = mailLogSnapshot();
}

static uint32_t maxClosedTransactionId(JsonArray sessions);

void session_mailer_save_settings(JsonObjectConst settings)
{
    const bool previousEnable = preferences.getBool(KEY_ENABLE, false);
    const uint8_t previousMode = preferences.getUChar(KEY_MODE, 0);
    String securityValue = settings["security"] | "";
    mail_security_t security = mail_security_t::starttls;
    if (securityValue == "none") security = mail_security_t::none;
    else if (securityValue == "ssl") security = mail_security_t::ssl;
    else if (securityValue.length() == 0 && settings["ssl"].is<bool>()) {
        security = (settings["ssl"] | true) ? mail_security_t::ssl : mail_security_t::none;
    }

    const uint16_t defaultPort = security == mail_security_t::starttls ? 587 : 465;
    preferences.putString(KEY_SERVER, settings["server"] | "smtp.world4you.com");
    preferences.putUShort(KEY_PORT, settings["port"] | defaultPort);
    preferences.putUChar(KEY_SECURITY, static_cast<uint8_t>(security));
    preferences.putBool(KEY_SSL, security == mail_security_t::ssl);
    preferences.putString(KEY_USER, settings["username"] | "report@innocharge.at");
    String password = settings["password"] | "";
    if (password.length() > 0) preferences.putString(KEY_PASS, password);
    preferences.putString(KEY_FROM, settings["from"] | "report@innocharge.at");
    preferences.putString(KEY_TO, settings["to"] | "");
    preferences.putString(KEY_SUBJECT, settings["subject"] | "InnoCharge charge sessions");
    const bool newEnable = settings["enable"] | false;
    const uint8_t newMode = settings["mode"] | 0;
    preferences.putBool(KEY_ENABLE, newEnable);
    preferences.putUChar(KEY_MODE, newMode);

    if (newEnable && newMode == 1) {
        const bool enteringAfterEach = !previousEnable || previousMode != 1;
        if (enteringAfterEach || !preferences.isKey(KEY_LAST_EACH_TX)) {
            JsonDocument doc;
            if (!deserializeJson(doc, loadMailerSessionsJson())) {
                preferences.putUInt(
                    KEY_LAST_EACH_TX,
                    maxClosedTransactionId(doc["sessions"].as<JsonArray>()));
                preferences.putBool(KEY_EACH_INITIALIZED, true);
            }
        } else if (!preferences.getBool(KEY_EACH_INITIALIZED, false)) {
            // Migration from firmware versions that only stored mailEachTx.
            preferences.putBool(KEY_EACH_INITIALIZED, true);
        }
    } else if (!newEnable || newMode != 1) {
        preferences.putBool(KEY_EACH_INITIALIZED, false);
    }
}

static bool sendReport(const mail_settings_t& settings, JsonArray sessions, const String& reportLabel)
{
    s_lastStatus = "";
    if (settings.server.length() == 0 || settings.port == 0 || settings.from.length() == 0 || settings.to.length() == 0) {
        s_lastStatus = "Missing SMTP settings";
        appendMailLog(false, reportLabel, sessions.size(), settings.to, s_lastStatus);
        return false;
    }

    if (sessions.size() == 0) {
        s_lastStatus = "No closed sessions to send";
        appendMailLog(false, reportLabel, 0, settings.to, s_lastStatus);
        return false;
    }

    String body = buildBody(sessions, reportLabel);
    String csv = buildCsv(sessions);

    IPAddress serverIp;
    if (!resolveServer(settings.server, serverIp)) {
        s_lastStatus = "DNS failed for " + settings.server;
        appendMailLog(false, reportLabel, sessions.size(), settings.to, s_lastStatus);
        return false;
    }

    bool ok = false;
    if (settings.security == mail_security_t::ssl) {
        WiFiClientSecure client;
        client.setInsecure();
        if (client.connect(serverIp, settings.port)) {
            ok = sendReportWithClient(client, settings, body, csv);
        } else {
            s_lastStatus = "SMTP connect failed";
        }
        client.stop();
    } else if (settings.security == mail_security_t::starttls) {
        StartTlsClient client;
        if (client.connect(serverIp, settings.port)) {
            ok = sendReportWithStartTls(client, settings, body, csv);
        } else {
            s_lastStatus = "SMTP connect failed";
        }
        client.stop();
    } else {
        WiFiClient client;
        if (client.connect(serverIp, settings.port)) {
            ok = sendReportWithClient(client, settings, body, csv);
        } else {
            s_lastStatus = "SMTP connect failed";
        }
        client.stop();
    }

    if (ok) {
        s_lastStatus = "Report sent (" + reportLabel + ")";
    } else if (s_lastStatus.length() == 0) {
        s_lastStatus = "Report failed (" + reportLabel + ")";
    }
    appendMailLog(ok, reportLabel, sessions.size(), settings.to, s_lastStatus);
    return ok;
}

bool session_mailer_send_manual_report()
{
    if (s_mailerBusy) {
        s_lastStatus = "Mailer busy";
        return false;
    }

    s_mailerBusy = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, loadMailerSessionsJson());
    if (err) {
        s_lastStatus = "Session JSON error";
        s_mailerBusy = false;
        return false;
    }

    JsonArray allSessions = doc["sessions"].as<JsonArray>();
    JsonDocument filtered = filteredSessions(allSessions, 0, 0, 0, nullptr, nullptr);
    bool ok = sendReport(loadSettings(), filtered["sessions"].as<JsonArray>(), "manual");
    s_mailerBusy = false;
    return ok;
}

static uint32_t maxClosedTransactionId(JsonArray sessions)
{
    uint32_t maxTx = 0;
    for (JsonObject session : sessions) {
        if (session["active"] | false) continue;
        uint32_t tx = session["transactionId"] | 0;
        if (tx > maxTx) maxTx = tx;
    }
    return maxTx;
}

void session_mailer_run_automatic()
{
    if (s_mailerBusy) return;

    mail_settings_t settings = loadSettings();
    if (!settings.enable || settings.mode == 0) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, loadMailerSessionsJson());
    if (err) {
        s_lastStatus = "Session JSON error";
        return;
    }

    JsonArray allSessions = doc["sessions"].as<JsonArray>();

    s_mailerBusy = true;

    if (settings.mode == 1) {
        if (!preferences.getBool(KEY_EACH_INITIALIZED, false)) {
            if (!preferences.isKey(KEY_LAST_EACH_TX)) {
                preferences.putUInt(KEY_LAST_EACH_TX, maxClosedTransactionId(allSessions));
            }
            preferences.putBool(KEY_EACH_INITIALIZED, true);
            s_mailerBusy = false;
            return;
        }

        uint32_t lastTx = preferences.getUInt(KEY_LAST_EACH_TX, 0);
        uint32_t maxTx = 0;
        JsonDocument filtered = filteredSessions(allSessions, lastTx, 0, 0, &maxTx, nullptr);
        if (filtered["sessions"].as<JsonArray>().size() > 0 && sendReport(settings, filtered["sessions"].as<JsonArray>(), "after each session")) {
            preferences.putUInt(KEY_LAST_EACH_TX, maxTx);
        }
        s_mailerBusy = false;
        return;
    }

    const time_t now = time(nullptr);
    if (now < 1700000000) {
        s_lastStatus = "Time not valid";
        s_mailerBusy = false;
        return;
    }

    const uint32_t intervalSeconds =
        settings.mode == 2 ? 86400UL :
        settings.mode == 3 ? 604800UL :
        2592000UL;
    const char* key =
        settings.mode == 2 ? KEY_LAST_DAILY :
        settings.mode == 3 ? KEY_LAST_WEEKLY :
        KEY_LAST_MONTHLY;
    const String label =
        settings.mode == 2 ? "daily" :
        settings.mode == 3 ? "weekly" :
        "monthly";

    time_t lastReport = (time_t)preferences.getULong64(key, 0);
    if (lastReport == 0) {
        lastReport = now - intervalSeconds;
    }

    if ((uint32_t)(now - lastReport) < intervalSeconds) {
        s_mailerBusy = false;
        return;
    }

    JsonDocument filtered = filteredSessions(allSessions, 0, lastReport, now, nullptr, nullptr);
    if (filtered["sessions"].as<JsonArray>().size() > 0 && sendReport(settings, filtered["sessions"].as<JsonArray>(), label)) {
        preferences.putULong64(key, (uint64_t)now);
    }

    s_mailerBusy = false;
}
