/**
 * @file time.cpp
 * @brief Time utility implementations - rollover projection, calendar/UNIX
 *        conversion, and timestamp formatting.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "libbotanik/hal/platform.h"
#include "libbotanik/utils/time.h"

namespace Botanik { namespace Utils {

    // Rollover projection
    // ------------------------------------------------------------------------

    uint64_t _project_rollover(uint64_t uptime, uint32_t timestamp) {
        int32_t diff = (int32_t)(timestamp - (uint32_t)(uptime & 0xFFFFFFFFULL));
        return (uint64_t)((int64_t)uptime + diff);
    }

    uint64_t absolute_ms(uint32_t timestamp_ms) {
        return _project_rollover(HAL::uptime_ms(), timestamp_ms);
    }

    uint64_t absolute_us(uint32_t timestamp_us) {
        return _project_rollover(HAL::uptime_us(), timestamp_us);
    }

    // Calendar conversion & formatting
    // ------------------------------------------------------------------------

    // Wall-clock epoch as UNIX ms at uptime 0, captured once. `wall_now -
    // uptime_now` is invariant (both advance together), so sampling it lazily
    // is fine.
    static int64_t _boot_epoch_ms() {
        static const int64_t epoch = (int64_t)HAL::wall_clock_ms() - (int64_t)HAL::uptime_ms();
        return epoch;
    }

    static uint64_t _wall_ms(uint64_t timestamp_ms) {
        return (uint64_t)(_boot_epoch_ms() + (int64_t)timestamp_ms);
    }

    // Domain-crossing helpers (uptime ms <-> UNIX ms), inverse of each other.
    uint64_t unix_ms_from_uptime_ms(uint64_t uptime_ms) {
        return _wall_ms(uptime_ms);
    }
    uint64_t uptime_ms_from_unix_ms(uint64_t unix_ms) {
        return (uint64_t)((int64_t)unix_ms - _boot_epoch_ms());
    }

    // Convert absolute timestamp to UTC calendar datetime.
    CalendarTime cal_time_ms(uint64_t timestamp_ms) {
        uint64_t wall_ms     = _wall_ms(timestamp_ms);
        uint64_t total_secs  = wall_ms / 1000;
        uint64_t secs_of_day = total_secs % 86400;
        // days since 1970-01-01
        int64_t  days        = (int64_t)(total_secs / 86400);

        // pure-integer UTC construction through Howard Hinnant's algorithm
        int64_t  z   = days + 719468;
        int64_t  era = (z >= 0 ? z : z - 146096) / 146097;
        uint32_t doe = (uint32_t)(z - era * 146097);                     // [0, 146096]
        uint32_t yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;  // [0, 399]
        uint32_t doy = doe - (365*yoe + yoe/4 - yoe/100);                // [0, 365]
        uint32_t mp  = (5*doy + 2) / 153;                                // [0, 11]
        uint32_t d   = doy - (153*mp + 2)/5 + 1;                         // [1, 31]
        uint32_t m   = mp < 10 ? mp + 3 : mp - 9;                        // [1, 12]
        int64_t  y   = (int64_t)yoe + era * 400 + (m <= 2);

        CalendarTime result;
        result.year        = (uint16_t)y;
        result.month       = (uint8_t)m;
        result.day         = (uint8_t)d;
        result.hour        = (uint8_t)(secs_of_day / 3600);
        result.minute      = (uint8_t)((secs_of_day % 3600) / 60);
        result.second      = (uint8_t)(secs_of_day % 60);

        // sub-second straight from the wall-clock ms (a real boot epoch, so
        // even boot-time entries get their true fraction).
        result.millisecond = (uint16_t)(wall_ms % 1000);
        result.unixSeconds = total_secs;
        result.unixDays    = (uint32_t)days;
        return result;
    }

    // Format a millisecond timestamp to a fixed-length string.
    void format_ms_timestamp(uint64_t timestamp_ms, char* buf, size_t len) {
        CalendarTime time_info = cal_time_ms(timestamp_ms);
        snprintf(
            buf, len, "%02u/%02u/%02u %02u:%02u:%02u.%03u",
            time_info.year % 100,
            time_info.month, time_info.day,
            time_info.hour, time_info.minute, time_info.second, time_info.millisecond
        );
    }

}}
