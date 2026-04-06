#ifndef UTILS_TIME_H
#define UTILS_TIME_H

#include <cstdint>
#include <array>
#include <ctime>
#include <string>
#include <cmath>

#include "libbotanik/hal/platform.h"
#include "libbotanik/utils/time.h"

using namespace Botanik::HAL;

namespace Botanik { namespace Utils {

    static uint64_t _project_rollover(uint64_t uptime, uint32_t timestamp) {
        uint32_t current = uptime % (1ULL << 32);
        uptime -= current;
        if (current - timestamp > (1ULL << 31)) uptime -= (1ULL << 32);
        if (timestamp - current > (1ULL << 31)) uptime += (1ULL << 32);
        return uptime + timestamp;
    }

    uint64_t absolute_ms(uint32_t timestamp_ms) { 
        return _project_rollover(uptime_ms(), timestamp_ms);
    }

    uint64_t absolute_us(uint32_t timestamp_us) {
        return _project_rollover(uptime_us(), timestamp_us);
    }

    time_t unix_time_ms(uint64_t timestamp_ms, uint16_t* frac_ms) {
        int64_t delta_ms = timestamp_ms - uptime_ms();
        time_t delta_s = (time_t)(delta_ms / 1000);
        if (frac_ms != nullptr) *frac_ms = (uint16_t)abs(delta_ms % 1000);
        if (delta_ms < 0 && *frac_ms > 0) delta_s--;
        return time(NULL) + delta_s;
    }

    CalendarTime cal_time_ms(uint64_t timestamp_ms) {
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

    void format_ms_timestamp(uint64_t timestamp_ms, std::array<char, 22>* buf) {
        CalendarTime time_info = cal_time_ms(timestamp_ms);
        snprintf(
            buf->data(), 22, "%02u/%02u/%02u %02u:%02u:%02u.%03u", 
            time_info.year % 100, time_info.month, time_info.day,
            time_info.hour, time_info.minute, time_info.second, time_info.millisecond
        );
    }

}}

#endif // UTILS_TIME_H