#include <cstdint>
#include <array>
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

    time_t unix_time_ms(uint64_t timestamp_ms) {
        // Uptime ms -> UNIX seconds. No boot epoch is stored, so anchor off "now":
        // shift the current wall second (time(NULL)) by the whole-second distance
        // between the timestamp and the current uptime. The sub-second part of the
        // wall clock is unavailable at this resolution and is carried separately
        // (see cal_time_ms).
        int64_t now_s   = (int64_t)(HAL::uptime_ms() / 1000);
        int64_t stamp_s = (int64_t)(timestamp_ms     / 1000);
        return time(NULL) + (time_t)(stamp_s - now_s);
    }

    CalendarTime cal_time_ms(uint64_t timestamp_ms) {
        tm time_info; CalendarTime result;
        time_t time_unix = unix_time_ms(timestamp_ms);
        localtime_r(&time_unix, &time_info);
        result.year = (uint16_t)(time_info.tm_year + 1900);
        result.month = (uint8_t)(time_info.tm_mon + 1);
        result.day = (uint8_t)time_info.tm_mday;
        result.hour = (uint8_t)time_info.tm_hour;
        result.minute = (uint8_t)time_info.tm_min;
        result.second = (uint8_t)time_info.tm_sec;
        // Sub-second is simply the timestamp's own remainder — no conversion needed.
        result.millisecond = (uint16_t)(timestamp_ms % 1000);
        result.unixSeconds = (uint64_t)time_unix;
        result.unixDays = (uint16_t)(time_unix / (uint64_t)86400);
        return result;
    }

    void format_ms_timestamp(uint64_t timestamp_ms, std::array<char, 22>* buf) {
        CalendarTime time_info = cal_time_ms(timestamp_ms);
        snprintf(
            buf->data(), 22, "%02u/%02u/%02u %02u:%02u:%02u.%03u",
            time_info.year % 100, // 2-digit year
            time_info.month, time_info.day,
            time_info.hour, time_info.minute, time_info.second, time_info.millisecond
        );
    }

}}
