#ifndef LOGS_FORMAT_H
#define LOGS_FORMAT_H

/**
 * @file format.h
 * @brief Log-entry formatting: assemble the `TIMESTAMP LEVEL TOPIC MESSAGE`
 *        line.
 */

#include <cstdint>
#include <cstdio>
#include <algorithm>
#include "libbotanik/plcio/vars.h"
#include "libbotanik/utils/time.h"

namespace Botanik { namespace Logs {

    inline const char* format_log_level(PlcIO::LogLevel level) { switch (level) {
        case PlcIO::LOG_DEBUG:      return "[~]";
        case PlcIO::LOG_INFO:       return "[-]";
        case PlcIO::LOG_NOTICE:     return "[+]";
        case PlcIO::LOG_WARNING:    return "[!]";
        case PlcIO::LOG_ERROR:      return "[*]";
        case PlcIO::LOG_CRITICAL:   return "[@]";
        default:                    return "";
    }; }

    inline const char* format_log_topic(PlcIO::LogTopic topic) { switch (topic) {
        case PlcIO::NONE:       return "";
        case PlcIO::BOOT:       return "BOOT";
        case PlcIO::NETWORK:    return "NETWORK";
        case PlcIO::TIME:       return "TIME";
        case PlcIO::STATUS:     return "STATUS";
        case PlcIO::PLC_IO:     return "SHARED_IO";
        case PlcIO::ASSERT:     return "ASSERT";
        case PlcIO::ALARM_START: return "ALARM_START";
        case PlcIO::ALARM_END:   return "ALARM_END";
        case PlcIO::ALARM_FLAP:  return "ALARM_FLAP";
        case PlcIO::HEALTH:      return "HEALTH";
        default:                return "";
    }; }

    /** @brief Format a complete log entry as `TIMESTAMP LEVEL TOPIC MESSAGE`.
     *  @return Number of bytes written, clamped to @p len; 0 on encoding
     *  error. */
    inline size_t format_log_entry(const PlcIO::LogEntry_t* entry, char* buf, size_t len) {
        using namespace Botanik::Utils;
        const char* level = format_log_level(entry->level);
        const char* topic = format_log_topic(entry->topic);
        // Skip the colon for the empty (NONE) topic, no dangling " : ".
        const char* topic_sep = (topic[0] != '\0') ? ":" : "";
        char time_buf[TIMESTAMP_LEN];
        format_ms_timestamp(absolute_ms(entry->timestamp), time_buf, sizeof(time_buf));
        int written = snprintf(buf, len, "%s %s %s%s %s",
                               time_buf, level, topic, topic_sep, entry->message);
        if (written < 0) return 0;
        return std::min((size_t)written, len);
    };

}}

#endif // LOGS_FORMAT_H