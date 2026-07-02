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

    inline const char* format_log_level(Logs::LogLevel level) { switch (level) {
        case Logs::LOG_DEBUG:      return "[~]";
        case Logs::LOG_INFO:       return "[-]";
        case Logs::LOG_NOTICE:     return "[+]";
        case Logs::LOG_WARNING:    return "[!]";
        case Logs::LOG_ERROR:      return "[*]";
        case Logs::LOG_CRITICAL:   return "[@]";
        default:                    return "";
    }; }

    inline const char* format_log_topic(Logs::LogTopic topic) { switch (topic) {
        case Logs::NONE:       return "";
        case Logs::BOOT:       return "BOOT";
        case Logs::NETWORK:    return "NETWORK";
        case Logs::TIME:       return "TIME";
        case Logs::STATUS:     return "STATUS";
        case Logs::PLC_IO:     return "SHARED_IO";
        case Logs::ASSERT:     return "ASSERT";
        case Logs::ALARM_START: return "ALARM_START";
        case Logs::ALARM_END:   return "ALARM_END";
        case Logs::ALARM_FLAP:  return "ALARM_FLAP";
        case Logs::HEALTH:      return "HEALTH";
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