#include "session_mailer.hpp"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <lwip/netdb.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "charge_session_log.hpp"

extern Preferences preferences;

static constexpr const char* KEY_SERVER = "mailServer";
static constexpr const char* KEY_PORT = "mailPort";
static constexpr const char* KEY_SSL = "mailSsl";
static constexpr const char* KEY_USER = "mailUser";
static constexpr const char* KEY_PASS = "mailPass";
static constexpr const char* KEY_FROM = "mailFrom";
static constexpr const char* KEY_TO = "mailTo";
static constexpr const char* KEY_SUBJECT = "mailSubject";
static constexpr const char* KEY_ENABLE = "mailEnable";
static constexpr const char* KEY_MODE = "mailMode";
static constexpr const char* KEY_LAST_EACH_TX = "mailEachTx";
static constexpr const char* KEY_LAST_DAILY = "mailLastDay";
static constexpr const char* KEY_LAST_WEEKLY = "mailLastWeek";
static constexpr const char* KEY_LAST_MONTHLY = "mailLastMonth";

static String s_lastStatus = "";
static volatile bool s_mailerBusy = false;

static String loadMailerSessionsJson()
{
    return charge_session_log_to_json_page(0, 100);
}

struct mail_settings_t {
    String server;
    uint16_t port = 465;
    bool ssl = true;
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
    settings.server = readPrefString(KEY_SERVER);
    settings.port = preferences.getUShort(KEY_PORT, 465);
    settings.ssl = preferences.getBool(KEY_SSL, true);
    settings.username = readPrefString(KEY_USER);
    settings.password = readPrefString(KEY_PASS);
    settings.from = readPrefString(KEY_FROM);
    settings.to = readPrefString(KEY_TO);
    settings.subject = readPrefString(KEY_SUBJECT, "InnoCharge charge sessions");
    settings.enable = preferences.getBool(KEY_ENABLE, false);
    settings.mode = preferences.getUChar(KEY_MODE, 0);
    return settings;
}

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

static bool sendReportWithClient(Client& client, const mail_settings_t& settings, const String& body, const String& csv)
{
    client.setTimeout(10000);

    if (!readSmtpResponse(client, 220)) return false;
    if (!sendCommand(client, "EHLO innocharge.local", 250)) return false;

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
    mailer["ssl"] = settings.ssl;
    mailer["username"] = settings.username;
    mailer["from"] = settings.from;
    mailer["to"] = settings.to;
    mailer["subject"] = settings.subject;
    mailer["enable"] = settings.enable;
    mailer["mode"] = settings.mode;
    mailer["passwordSet"] = settings.password.length() > 0;
    mailer["lastStatus"] = s_lastStatus;
}

void session_mailer_save_settings(JsonObjectConst settings)
{
    preferences.putString(KEY_SERVER, settings["server"] | "");
    preferences.putUShort(KEY_PORT, settings["port"] | 465);
    preferences.putBool(KEY_SSL, settings["ssl"] | true);
    preferences.putString(KEY_USER, settings["username"] | "");
    String password = settings["password"] | "";
    if (password.length() > 0) preferences.putString(KEY_PASS, password);
    preferences.putString(KEY_FROM, settings["from"] | "");
    preferences.putString(KEY_TO, settings["to"] | "");
    preferences.putString(KEY_SUBJECT, settings["subject"] | "InnoCharge charge sessions");
    preferences.putBool(KEY_ENABLE, settings["enable"] | false);
    preferences.putUChar(KEY_MODE, settings["mode"] | 0);
}

static bool sendReport(const mail_settings_t& settings, JsonArray sessions, const String& reportLabel)
{
    if (settings.server.length() == 0 || settings.port == 0 || settings.from.length() == 0 || settings.to.length() == 0) {
        s_lastStatus = "Missing SMTP settings";
        return false;
    }

    if (sessions.size() == 0) {
        s_lastStatus = "No closed sessions to send";
        return false;
    }

    String body = buildBody(sessions, reportLabel);
    String csv = buildCsv(sessions);

    IPAddress serverIp;
    if (!resolveServer(settings.server, serverIp)) {
        s_lastStatus = "DNS failed for " + settings.server;
        return false;
    }

    bool ok = false;
    if (settings.ssl) {
        WiFiClientSecure client;
        client.setInsecure();
        if (client.connect(serverIp, settings.port)) {
            ok = sendReportWithClient(client, settings, body, csv);
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
        s_lastStatus = "Manual report sent";
    } else if (s_lastStatus.length() == 0 || s_lastStatus == "Manual report sent") {
        s_lastStatus = "Manual report failed";
    }
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
        uint32_t lastTx = preferences.getUInt(KEY_LAST_EACH_TX, 0);
        if (lastTx == 0) {
            preferences.putUInt(KEY_LAST_EACH_TX, maxClosedTransactionId(allSessions));
            s_mailerBusy = false;
            return;
        }

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
