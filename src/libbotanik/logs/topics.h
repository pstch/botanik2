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

#include <cstdint>              // uint8_t

#include "libbotanik/config/logging.h"    // Config::LOG_*

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
    };

    /** @brief Number of defined topics (the enum is dense 0..STORAGE). Bounds
     *  the logger's per-tick scan loops and sizes the per-topic bucket array. */
    static constexpr uint8_t LOG_TOPIC_COUNT = STORAGE + 1;

    // The wire format and every bucket-loop counter are uint8_t.
    static_assert(LOG_TOPIC_COUNT <= 255, "LOG_TOPIC_COUNT exceeds the range of LogTopic");

}}

#endif // LIBBOTANIK_LOGS_TOPICS_H
