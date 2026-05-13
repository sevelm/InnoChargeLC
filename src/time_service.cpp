#include "time_service.hpp"

#include <time.h>
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
