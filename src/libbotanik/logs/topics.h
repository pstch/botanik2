// REVIEW_35: TODO
#ifndef LIBBOTANIK_LOGS_TOPICS_H
#define LIBBOTANIK_LOGS_TOPICS_H

/**
 * @file topics.h
 * @brief Log severity levels and topics.
 *
 * The numbering is kept as-is rather than compacted: it is what sits in
 * already-written log records, so a rename moves the label and never the value.
 */

#include <cstdint>

#include "libbotanik/config/logging.h"

namespace Botanik { namespace Logs {

    /** @brief Log severity level. */
    enum LogLevel : uint8_t {
        LOG_DEBUG = 1,
        LOG_INFO = 2,
        LOG_NOTICE = 4,
        LOG_WARNING = 8,
        LOG_ERROR = 16,
        LOG_CRITICAL = 32,
    };

    /** @brief A level's lowercase word, or NULL for anything that is not one.
     *
     *  The T_ENUM converter behind every `*_log_level` parameter (declared with
     *  PE() in library_vars.h), so it is what the operator both reads and types.
     *  NULL is load-bearing there: it is how the variable model knows 3 is not a
     *  level, since these are bit values and not a run.
     *
     *  Beside the enum rather than in logs/format.h with the display helpers: a
     *  site's variable table pulls this in, and that table has no business
     *  seeing the line formatter. */
    inline const char* level_label(uint64_t v) {
        switch (v) {
            case LOG_DEBUG:    return "debug";
            case LOG_INFO:     return "info";
            case LOG_NOTICE:   return "notice";
            case LOG_WARNING:  return "warning";
            case LOG_ERROR:    return "error";
            case LOG_CRITICAL: return "critical";
            default:           return nullptr;
        }
    }

    // Listed in ascending value order; the enum must stay dense from 0, since
    // LOG_TOPIC_COUNT bounds the logger's per-topic bucket array.
    /** @brief Log topic / category. */
    enum LogTopic : uint8_t {
        NONE = 0,
        MODBUS = 1,        // the field bus
        BOOT = 2,
        NETWORK = 3,
        TIME = 4,
        STATUS = 5,
        ASSERT = 6,        // firmware assertions
        // BEG spelled to ALARM_END's width, so the topic column lines up.
        ALARM_BEG = 7,     // alarm went active
        ALARM_END = 8,     // alarm cleared
        ALARM_FLAP = 9,    // cleared without ever logging a START (too fast to dwell)
        HEALTH = 10,       // HealthMonitor (POST verdict, etc.)
        STORAGE = 11,      // persistence; its own topic, not a boot problem
        USER = 12,         // written by an operator, through the `log` command
        // The two observability topics. DEBUG-level and high-volume by nature,
        // which is exactly why they are their own topics: a sink can carry the
        // control logic's trace without carrying everything else at DEBUG.
        PROGRAM = 13,      // a control program changed state
        QUALITY = 14,      // a variable's validity changed
    };

    /** @brief Number of defined topics (the enum is dense 0..USER). Bounds the
     *  logger's per-tick scan loops and sizes the per-topic bucket array. */
    static constexpr uint8_t LOG_TOPIC_COUNT = QUALITY + 1;

    // The wire format and every bucket-loop counter are uint8_t.
    static_assert(LOG_TOPIC_COUNT <= 255, "LOG_TOPIC_COUNT exceeds the range of LogTopic");

}}

#endif // LIBBOTANIK_LOGS_TOPICS_H
