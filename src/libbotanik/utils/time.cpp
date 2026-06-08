/**
 * @file time.cpp
 * @brief Time utility implementations — rollover projection, calendar/UNIX
 *        conversion, and timestamp formatting.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>

#include "libbotanik/hal/platform.h"
#include "libbotanik/utils/time.h"

namespace Botanik { namespace Utils {

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

    // Wall-clock epoch as UNIX ms at uptime 0, captured once on first use. Since
    // wall and uptime advance together, (wall_now - uptime_now) is invariant, so
    // sampling it lazily is fine; caching pins every timestamp to one stable
    // reference and avoids per-call drift between the two clocks.
    static int64_t _boot_epoch_ms() {
        static const int64_t epoch = (int64_t)HAL::wall_clock_ms() - (int64_t)HAL::uptime_ms();
        return epoch;
    }

    // Map an uptime-domain ms timestamp onto wall-clock UNIX ms.
    static uint64_t _wall_ms(uint64_t timestamp_ms) {
        return (uint64_t)(_boot_epoch_ms() + (int64_t)timestamp_ms);
    }

    time_t unix_time_ms(uint64_t timestamp_ms) {
        return (time_t)(_wall_ms(timestamp_ms) / 1000);
    }

    CalendarTime cal_time_ms(uint64_t timestamp_ms) {
        uint64_t wall_ms = _wall_ms(timestamp_ms);
        time_t   time_unix = (time_t)(wall_ms / 1000);
        tm time_info; CalendarTime result;
        localtime_r(&time_unix, &time_info);
        result.year = (uint16_t)(time_info.tm_year + 1900);
        result.month = (uint8_t)(time_info.tm_mon + 1);
        result.day = (uint8_t)time_info.tm_mday;
        result.hour = (uint8_t)time_info.tm_hour;
        result.minute = (uint8_t)time_info.tm_min;
        result.second = (uint8_t)time_info.tm_sec;
        // Sub-second comes straight from the wall-clock ms now that we carry a
        // real boot epoch — so even boot-time entries get their true fraction.
        result.millisecond = (uint16_t)(wall_ms % 1000);
        result.unixSeconds = (uint64_t)time_unix;
        result.unixDays = (uint16_t)(time_unix / (uint64_t)86400);
        return result;
    }

    void format_ms_timestamp(uint64_t timestamp_ms, char* buf, size_t len) {
        CalendarTime time_info = cal_time_ms(timestamp_ms);
        snprintf(
            buf, len, "%02u/%02u/%02u %02u:%02u:%02u.%03u",
            time_info.year % 100, // 2-digit year
            time_info.month, time_info.day,
            time_info.hour, time_info.minute, time_info.second, time_info.millisecond
        );
    }

}}
