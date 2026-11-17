#include "time_service.hpp"

#include <time.h>
#include <sys/time.h>
#include <limits>
#include "AA_globals.h"

static constexpr const char* TIMEZONE_CET_CEST = "CET-1CEST,M3.5.0/2,M10.5.0/3";
static constexpr const char* DEFAULT_NTP_1 = "pool.ntp.org";
static constexpr const char* DEFAULT_NTP_2 = "time.nist.gov";
static constexpr const char* DEFAULT_NTP_3 = "time.google.com";

static void apply_ntp_settings()
{
    configTzTime(
        TIMEZONE_CET_CEST,
        DEFAULT_NTP_1,
        DEFAULT_NTP_2,
        DEFAULT_NTP_3);
}

void time_service_begin()
{
    apply_ntp_settings();
}

void time_service_request_sync()
{
    apply_ntp_settings();
}

bool time_service_is_valid()
{
    time_t now = time(nullptr);
    return now > 1700000000;
}

bool time_service_set_epoch(uint32_t epoch)
{
    // Do not wrap time_t on SDKs with a 32-bit system clock.
    if (epoch <= 1700000000 || uint64_t(epoch) > uint64_t(std::numeric_limits<time_t>::max()))
        return false;
    struct timeval value = {};
    value.tv_sec = static_cast<time_t>(epoch);
    // SNTP stays enabled and can update the clock when Internet time is available.
    return settimeofday(&value, nullptr) == 0;
}

bool time_service_set_local(const char* localDateTime)
{
    if (!localDateTime || strlen(localDateTime) != 19) return false;
    int year, month, day, hour, minute, second, consumed = 0;
    if (sscanf(localDateTime, "%4d-%2d-%2dT%2d:%2d:%2d%n",
               &year, &month, &day, &hour, &minute, &second, &consumed) != 6 || consumed != 19 ||
        year < 2023 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59)
        return false;

    struct tm local = {};
    local.tm_year = year - 1900;
    local.tm_mon = month - 1;
    local.tm_mday = day;
    local.tm_hour = hour;
    local.tm_min = minute;
    local.tm_sec = second;
    local.tm_isdst = -1;
    const time_t epoch = mktime(&local); // Existing CET/CEST device timezone.
    if (epoch <= 1700000000 || uint64_t(epoch) > UINT32_MAX) return false;

    // mktime normalizes invalid dates and DST gaps; reject those instead.
    struct tm check = {};
    if (!localtime_r(&epoch, &check) || check.tm_year != year - 1900 || check.tm_mon != month - 1 ||
        check.tm_mday != day || check.tm_hour != hour || check.tm_min != minute || check.tm_sec != second)
        return false;
    return time_service_set_epoch(static_cast<uint32_t>(epoch));
}

String time_service_local_string()
{
    if (!time_service_is_valid()) return "Zeit noch nicht synchronisiert";

    time_t now = time(nullptr);
    struct tm tmLocal;
    localtime_r(&now, &tmLocal);

    char buf[32];
    strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M:%S", &tmLocal);
    return String(buf);
}

String time_service_iso_utc_string()
{
    if (!time_service_is_valid()) return "";

    time_t now = time(nullptr);
    struct tm tmUtc;
    gmtime_r(&now, &tmUtc);

    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmUtc);
    return String(buf);
}
