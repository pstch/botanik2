#ifndef UTILS_TIME_H
#define UTILS_TIME_H

#include <cstdint>
#include <array>
#include <ctime>

namespace Botanik { namespace Utils {

    // These utilities provides some standard utility functions to work with the
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
    uint64_t _project_rollover(uint64_t uptime, uint32_t timestamp);

    // Project a rolling-over millisecond uptime counter into the current rollover window
    uint64_t absolute_ms(uint32_t timestamp_ms);

    // Project a rolling-over microsecond uptime counter into the current rollover window
    //   WARNING: see warning in uptime_ms() about the microsecond
    //                precision of the low-power clock
    uint64_t absolute_us(uint32_t timestamp_us);

    // Convert a monotonic uptime millisecond counter to a UNIX timestamp (whole
    // seconds), using a cached wall-clock boot epoch. See cal_time_ms() for the
    // sub-second part.
    time_t unix_time_ms(uint64_t timestamp_ms);

    // Convert an absolute millisecond uptime counter into a calendar datetime
    CalendarTime cal_time_ms(uint64_t timestamp_ms);

    // Format an absolute millisecond uptime counter into an UTC timestamp
    void format_ms_timestamp(uint64_t timestamp_ms, std::array<char, 22>* buf);

}}

#endif // UTILS_TIME_H