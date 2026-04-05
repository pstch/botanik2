#ifndef UTILS_TIME_H
#define UTILS_TIME_H

#include <cstdint>
#include <array>
#include <ctime>
#include <string>
#include <cmath>

#include "mbed.h"

// Definitions provided by the Arduino headers
uint32_t millis();
uint32_t micros();

// Duration conversions
// #define TIMEPOINT_TO_MS(x) (std::chrono::duration_cast<std::chrono::milliseconds>((x).time_since_epoch())).count()
// #define TIMEPOINT_TO_US(x) (std::chrono::duration_cast<std::chrono::microseconds>((x).time_since_epoch())).count()


// This namespace provides some standard utility functions to work with the 
// different ways we use to represent time :
//  - UNIX timestamps, stored as uint64_t on this platform
//  - absolute millisecond/microsecond uptime counters, stored as uint64_t
//  - rolling-over millisecond/microsecond uptime counters, stored as uint32_t
//
// Some components of our firmware record and transmit 32-bit rolling-over counters
// without additional context, and we need to project them into the current rollover
// window, assuming that they refer to events that occurred in a rollover window
// centered on current time. This namespace provides functions to handle this projection
// (absolute_ms() and absolute_us()), as well a some standard formatting/conversion functions.
namespace Botanix { namespace Utils { namespace TimeUtils {
    // Calendar time struct, without the idiosyncracies from the
    // standard tm struct and with some additional fields for total
    // days and milliseconds
    struct CalendarTime {
        // standard calendar fields
        uint16_t    year;               // years after BC
        uint8_t     month;              // month of current year, 1-indexed
        uint8_t     day;                // day of current month, 1-indexed
        uint8_t     hour;               // hour of current day, 0-indexed
        uint8_t     minute;             // minute of current hour, 0-indexed
        uint8_t     second;             // second of current minute, 0-indexed
        uint16_t    millisecond;        // millisecond of current second, 0-indexed
        // monotonic counters
        uint16_t    unixDays;           // days after UNIX epoch, 0-indexed
        uint64_t    unixSeconds;        // seconds after UNIX epoch, 0-indexed
    };
    // Convert a 32-bit uptime timestamp to an absolute 64-bit timestamp,
    // assuming the given timestamp occurs in the rollover window centered 
    // on the given absolute timestamp (71 minutes for microseconds, 
    // 49 days for milliseconds).
    static uint64_t _absolute_timestamp(uint64_t uptime, uint32_t timestamp) {
        uint32_t current = uptime % (1ULL << 32);
        uptime -= current;
        if (current - timestamp > (1ULL << 31)) uptime -= (1ULL << 32);
        if (timestamp - current > (1ULL << 31)) uptime += (1ULL << 32);
        return uptime + timestamp;
    }
    // Sawtooth millisecond uptime counter (rolls over every ~49 days), using the low-power clock
    //   (equivalent to Arduino's millis() function)
    static uint32_t current_ms()    { return millis(); }
    // Sawtooth microsecond uptime counter (rolls over every ~71 minutes), using the regular clock
    //   (equivalent to Arduino's micros() function)
    //   WARNING: this counter does not run when the device is in SLEEP mode, 
    //            it should only be used for short intervals during which the
    //            device is guaranteed to stay in RUN mode
    static uint32_t current_us()    { return micros(); }
    // Monotonic millisecond uptime counter (no rollover), using the low-power clock
    static uint64_t uptime_ms()     { return TIMEPOINT_TO_MS(mbed::LowPowerClock::now()); }
    // Monotonic microsecond uptime counter (no rollover), using the low-power clock
    //   WARNING: the low-power clock used for this counter will generally not
    //            have enough precision for true microseconds. On an Arduino OPTA, 
    //            this clock runs at 32.768 kHz, which means that the smallest interval
    //            it can measure is around ~30 microseconds.
    static uint64_t uptime_us()     { return TIMEPOINT_TO_US(mbed::LowPowerClock::now()); }
    // Project a rolling-over millisecond uptime counter into the current rollover window
    static uint64_t absolute_ms(uint32_t timestamp_ms) { 
        return _absolute_timestamp(uptime_ms(), timestamp_ms);
    }
    // Project a rolling-over microsecond uptime counter into the current rollover window
    //   WARNING: see warning in uptime_ms() about the microsecond 
    //                precision of the low-power clock
    static uint64_t absolute_us(uint32_t timestamp_us) {
        return _absolute_timestamp(uptime_us(), timestamp_us);
    }
    // Convert a monotonic uptime millisecond counter to an UNIX integer timestamp
    //  (and copy the fractional part to the pointer given in frac_ms)
    static time_t unix_time_ms(uint64_t timestamp_ms, uint16_t* frac_ms) {
        int64_t delta_ms = timestamp_ms - uptime_ms();
        time_t delta_s = (time_t)(delta_ms / 1000);
        if (frac_ms != nullptr) *frac_ms = (uint16_t)std::abs(delta_ms % 1000);
        if (delta_ms < 0 && *frac_ms > 0) delta_s--;
        return time(NULL) + delta_s;
    }
    // Convert an absolute millisecond uptime counter into a calendar datetime
    static CalendarTime cal_time_ms(uint64_t timestamp_ms) {
        uint16_t time_frac; tm time_info; CalendarTime result;
        time_t time_unix = unix_time_ms(timestamp_ms, &time_frac);
        localtime_r(&time_unix, &time_info);
        result.year = (uint16_t)(time_info.tm_year + 1900);
        result.month = (uint8_t)(time_info.tm_mon + 1);
        result.day = (uint8_t)time_info.tm_mday;
        result.hour = (uint8_t)time_info.tm_hour;
        result.minute = (uint8_t)time_info.tm_min;
        result.second = (uint8_t)time_info.tm_sec;
        result.millisecond = time_frac;
        result.unixSeconds = (uint64_t)time_unix;
        result.unixDays = (uint16_t)(time_unix / (uint64_t)86400);
        return result;
    }
    // Format an absolute millisecond uptime counter into an UTC timestamp 
    void format_ms_timestamp(uint64_t timestamp_ms, std::array<char, 22>* buf) {
        CalendarTime time_info = cal_time_ms(timestamp_ms);
        snprintf(
            buf->data(), 22, "%02u/%02u/%02u %02u:%02u:%02u.%03u", 
            time_info.year % 100, time_info.month, time_info.day,
            time_info.hour, time_info.minute, time_info.second, time_info.millisecond
        );
    }

}}}

#endif // UTILS_TIME_H