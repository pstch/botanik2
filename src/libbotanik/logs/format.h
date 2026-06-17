#ifndef LOGS_FORMAT_H
#define LOGS_FORMAT_H

/**
 * @file format.h
 * @brief Log-entry formatting - render level/topic labels and assemble the
 *        `TIMESTAMP LEVEL TOPIC MESSAGE` line written by the log writers.
 */

#include <cstdint>
#include <cstdio>
#include <algorithm>
#include "libbotanik/plcio/vars.h"
#include "libbotanik/utils/time.h"

namespace Botanik { namespace Logs {

    /**
     * @brief Render a log level as a short bracketed indicator.
     */
    inline const char* format_log_level(PlcIO::LogLevel level) { switch (level) {
        case PlcIO::LOG_DEBUG:      return "[~]";
        case PlcIO::LOG_INFO:       return "[-]";
        case PlcIO::LOG_NOTICE:     return "[+]";
        case PlcIO::LOG_WARNING:    return "[!]";
        case PlcIO::LOG_ERROR:      return "[*]";
        case PlcIO::LOG_CRITICAL:   return "[@]";
        default:                    return "";
    }; }

    /**
     * @brief Render a log topic as a printable label.
     */
    inline const char* format_log_topic(PlcIO::LogTopic topic) { switch (topic) {
        case PlcIO::NONE:       return "";
        case PlcIO::BOOT:       return "BOOT";
        case PlcIO::NETWORK:    return "NETWORK";
        case PlcIO::TIME:       return "TIME";
        case PlcIO::STATUS:     return "STATUS";
        case PlcIO::PLC_IO:     return "SHARED_IO";
        default:                return "";
    }; }

    /**
     * @brief Format a complete log entry as `TIMESTAMP LEVEL TOPIC MESSAGE`.
     * @param entry Entry to format.
     * @param buf   Destination buffer.
     * @param len   Capacity of @p buf in bytes.
     * @return Number of bytes written, clamped to @p len; 0 on encoding error.
     */
    inline size_t format_log_entry(const PlcIO::LogEntry_t* entry, char* buf, size_t len) {
        using namespace Botanik::Utils;
        const char* level = format_log_level(entry->level);
        const char* topic = format_log_topic(entry->topic);
        // Colon belongs to the line layout, not the label; skip it for the empty
        // (NONE) topic so the line doesn't carry a dangling " : ".
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