// REVIEW_35: DONE
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
#include "entry.h"
#include "../utils/time.h"

namespace Botanik { namespace Logs {

    /** @brief Format a log level into a short prefix.  */
    inline const char* format_log_level(Logs::LogLevel level) { switch (level) {
        case Logs::LOG_DEBUG:      return "[~]";
        case Logs::LOG_INFO:       return "[-]";
        case Logs::LOG_NOTICE:     return "[+]";
        case Logs::LOG_WARNING:    return "[!]";
        case Logs::LOG_ERROR:      return "[*]";
        case Logs::LOG_CRITICAL:   return "[@]";
        default:                    return "[?]";
    }; }

    /** @brief Per-topic strings, indexed by LogTopic (dense 0..STORAGE):
     *  { display label, lowercase MQTT segment }. One table, so the two can no
     *  longer drift apart. */
    static const char* const TOPIC_NAMES[Logs::LOG_TOPIC_COUNT][2] = {
        { "",            "none"        },
        { "SHARED_IO",   "shared_io"   },
        { "BOOT",        "boot"        },
        { "NETWORK",     "network"     },
        { "TIME",        "time"        },
        { "STATUS",      "status"      },
        { "ASSERT",      "assert"      },
        { "ALARM_START", "alarm_start" },
        { "ALARM_END",   "alarm_end"   },
        { "ALARM_FLAP",  "alarm_flap"  },
        { "HEALTH",      "health"      },
        { "STORAGE",     "storage"     },
    };

    /** @brief Format a log topic into its actual name. */
    inline const char* format_log_topic(Logs::LogTopic topic) {
        return topic < Logs::LOG_TOPIC_COUNT ? TOPIC_NAMES[topic][0] : "";
    }

    /** @brief Lowercase level word, for MQTT topic segments. */
    inline const char* format_log_level_name(Logs::LogLevel level) { switch (level) {
        case Logs::LOG_DEBUG:      return "debug";
        case Logs::LOG_INFO:       return "info";
        case Logs::LOG_NOTICE:     return "notice";
        case Logs::LOG_WARNING:    return "warning";
        case Logs::LOG_ERROR:      return "error";
        case Logs::LOG_CRITICAL:   return "critical";
        default:                    return "unknown";
    }; }

    /** @brief Lowercase topic word, for MQTT topic segments (PLC_IO -> "shared_io",
     *  matching the SHARED_IO display label). */
    inline const char* format_log_topic_name(Logs::LogTopic topic) {
        return topic < Logs::LOG_TOPIC_COUNT ? TOPIC_NAMES[topic][1] : "unknown";
    }

    /** @brief Format a complete log entry as `TIMESTAMP LEVEL TOPIC MESSAGE`.
     *  @return Bytes written, clamped to @p len; 0 on encoding error. */
    inline size_t format_log_entry(const Logs::LogEntry* entry, char* buf, size_t len) {
        using namespace Botanik::Utils;
        // format the log level and log topic of the log entry
        const char* level = format_log_level(entry->level);
        const char* topic = format_log_topic(entry->topic);
        // skip the separator if the topic is empty
        const char* topic_sep = (topic[0] != '\0') ? ":" : "";
        // format the timestamp
        uint64_t timestamp = entry->timestamp;
        char time_buf[TIMESTAMP_LEN];
        format_ms_timestamp(timestamp, time_buf, sizeof(time_buf));
        // build the formatted log entry
        int written = snprintf(buf, len, "%s %s %s%s %s",
                               time_buf, level, topic, topic_sep, entry->message);
        // return the written bytes, or 0 on encoding error
        if (written < 0) return 0;
        return std::min((size_t)written, len);
    };

}}

#endif // LOGS_FORMAT_H