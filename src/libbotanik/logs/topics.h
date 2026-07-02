#ifndef LIBBOTANIK_LOGS_TOPICS_H
#define LIBBOTANIK_LOGS_TOPICS_H

/**
 * @file topics.h
 * @brief Log severity levels and topics.
 *
 * These are logging primitives, needed even in a PLC-less build, so they live
 * with the logger rather than in the (optional) PLC area. Their values are the
 * wire protocol shared with the PLC, though: the PLC log ring carries them
 * byte-for-byte (see PlcIO::LogEntry_t in plcio/vars.h), so changing a value is
 * a coordinated PLC-side change, not a local rename.
 */

#include <cstdint>

#include "libbotanik_config.h"

namespace Botanik { namespace Logs {

    /** @brief Log severity level (wire value shared with the PLC). */
    enum LogLevel : uint8_t {
        LOG_DEBUG = 1,
        LOG_INFO = 2,
        LOG_NOTICE = 4,
        LOG_WARNING = 8,
        LOG_ERROR = 16,
        LOG_CRITICAL = 32,
    };

    // Values are the wire protocol shared with the PLC, not a sequence; listed
    // in ascending value order. Changing a value requires a matching PLC-side
    // change.
    /** @brief Log topic / category (wire value shared with the PLC). */
    enum LogTopic : uint8_t {
        NONE = 0,
        PLC_IO = 1,
        BOOT = 2,
        NETWORK = 3,
        TIME = 4,
        STATUS = 5,
        ASSERT = 6,        // C-side only (PLC never emits these): firmware assertions
        ALARM_START = 7,   // alarm went active
        ALARM_END = 8,     // alarm cleared
        // alarm cleared without ever logging a START (toggled too fast to
        // dwell)
        ALARM_FLAP = 9,
        HEALTH = 10,       // HealthMonitor (POST verdict, etc.)
    };

    // Check that the defined topic count can fit in the LogTopic enum
    static_assert(Config::LOG_TOPIC_MAX < (1 << (sizeof(LogTopic)*8)),
                  "LOG_TOPIC_MAX exceeds the range of the LogTopic enum");

    /** @brief Number of defined topics (the enum is dense 0..HEALTH). Bounds
     *  the logger's per-tick scan loops so they cover the real topics instead
     *  of the full wire range; the bucket array itself is sized by
     *  LOG_TOPIC_MAX. */
    static constexpr uint8_t LOG_TOPIC_COUNT = HEALTH + 1;
    static_assert(LOG_TOPIC_COUNT <= Config::LOG_TOPIC_MAX, "LOG_TOPIC_COUNT exceeds LOG_TOPIC_MAX");

}}

#endif // LIBBOTANIK_LOGS_TOPICS_H
