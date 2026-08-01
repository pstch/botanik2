// REVIEW_35: TODO
#ifndef LIBBOTANIK_LOGS_TOPICS_H
#define LIBBOTANIK_LOGS_TOPICS_H

/**
 * @file topics.h
 * @brief Log severity levels and topics.
 *
 * These are logging primitives, so they live with the logger. The values were
 * once a wire protocol shared with a coprocessor; that is gone, so a new topic
 * is now an ordinary local addition. The numbering is kept as-is rather than
 * compacted, because it is what sits in already-written log records.
 */

#include <cstdint>

#include "../../libbotanik_config.h"

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
        SHARED_IO = 1,
        BOOT = 2,
        NETWORK = 3,
        TIME = 4,
        STATUS = 5,
        ASSERT = 6,        // firmware assertions
        ALARM_START = 7,   // alarm went active
        ALARM_END = 8,     // alarm cleared
        // alarm cleared without ever logging a START (toggled too fast to
        // dwell)
        ALARM_FLAP = 9,
        HEALTH = 10,       // HealthMonitor (POST verdict, etc.)
        // Persistence. Its own topic because a storage failure is a distinct
        // fault with a distinct fix - it is not a boot problem, and burying it
        // under BOOT is how it went unnoticed that nothing persisted at all.
        STORAGE = 11,
    };

    // Check that the defined topic count can fit in the LogTopic enum
    static_assert(Config::LOG_TOPIC_MAX < (1 << (sizeof(LogTopic)*8)),
                  "LOG_TOPIC_MAX exceeds the range of the LogTopic enum");

    /** @brief Number of defined topics (the enum is dense 0..HEALTH). Bounds
     *  the logger's per-tick scan loops so they cover the real topics instead
     *  of the full wire range; the per-topic bucket array is sized by
     *  LOG_TOPIC_COUNT. */
    static constexpr uint8_t LOG_TOPIC_COUNT = STORAGE + 1;
    static_assert(LOG_TOPIC_COUNT <= Config::LOG_TOPIC_MAX, "LOG_TOPIC_COUNT exceeds LOG_TOPIC_MAX");

}}

#endif // LIBBOTANIK_LOGS_TOPICS_H
