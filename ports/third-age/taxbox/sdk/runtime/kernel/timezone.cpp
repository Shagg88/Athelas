// The console's time zone, from the PC's: the user settings block of the EEPROM (ExQueryNonVolatileSetting
// XC_MAX_OS, 0x100 bytes from EEPROM 0x60). XAPI's GetTimeZoneInformation reads it, and through it every
// FileTimeToLocalFileTime: without it the games show times in UTC (the Two Towers' save list, 0xE3690).
//   +0x04 bias, minutes (UTC = local + bias)    +0x08 / +0x0C standard / daylight name, 4 characters
//   +0x18 / +0x1C standard / daylight start: month, week of the month (1-5), day of the week, hour
//   +0x28 / +0x2C standard / daylight bias      +0x58 misc flags: bit 1 no daylight saving
#include <cstdint>
#include <cstring>
#include <ctime>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace kernel {

namespace {
void Put32(uint8_t* p, int32_t v) { std::memcpy(p, &v, 4); }

#ifdef _WIN32
// "Pacific Standard Time" -> "PST"
void Name(uint8_t* out, const WCHAR* name) {
    int n = 0;
    bool start = true;
    for (const WCHAR* c = name; *c && n < 4; c++) {
        if (*c == L' ') start = true;
        else if (start) {
            if ((*c >= L'A' && *c <= L'Z') || (*c >= L'a' && *c <= L'z')) out[n++] = uint8_t(*c);
            start = false;
        }
    }
}

void Date(uint8_t* out, const SYSTEMTIME& t) {
    out[0] = uint8_t(t.wMonth);
    out[1] = uint8_t(t.wDay);
    out[2] = uint8_t(t.wDayOfWeek);
    out[3] = uint8_t(t.wHour);
}
#endif
}  // namespace

void HostTimeZone(uint8_t* user) {
#ifdef _WIN32
    TIME_ZONE_INFORMATION tz{};
    if (GetTimeZoneInformation(&tz) == TIME_ZONE_ID_INVALID) return;
    Put32(user + 0x04, tz.Bias);
    Name(user + 0x08, tz.StandardName);
    Name(user + 0x0C, tz.DaylightName);
    if (tz.StandardDate.wMonth && tz.DaylightDate.wMonth && tz.StandardDate.wYear == 0) {
        Date(user + 0x18, tz.StandardDate);
        Date(user + 0x1C, tz.DaylightDate);
        Put32(user + 0x28, tz.StandardBias);
        Put32(user + 0x2C, tz.DaylightBias);
    } else {
        // no daylight saving (or a one-off rule the console can't keep): today's offset all year
        Put32(user + 0x04, tz.Bias + (GetTimeZoneInformation(&tz) == TIME_ZONE_ID_DAYLIGHT ? tz.DaylightBias : tz.StandardBias));
        user[0x58] |= 2;
    }
#else
    // the test harness: today's offset, no daylight saving rules
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    Put32(user + 0x04, int32_t(-local.tm_gmtoff / 60));
    user[0x58] |= 2;
#endif
}

}  // namespace kernel
