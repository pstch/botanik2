#ifndef LIBBOTANIK_CONFIG_LOGGING_H
#define LIBBOTANIK_CONFIG_LOGGING_H

/** @file logging.h
 *  @brief The logger's queues, rate limits and formatting widths. */

#include "libbotanik/config/base.h"

// Defined and undefined per file, not in base.h: see base.h.
#define BOTANIK_CFG(type, name) static constexpr type name = BOTANIK_##name

namespace Botanik { namespace Config {

    /** @name Logging
     *
     *  Message sizing, the per-writer queues, and the dual-bucket rate limiter
     *  in front of them.
     *  @{ */


    // What each sink shows is a PARAMETER, not a macro:
    // home_log_level, shell_log_level, logs_log_level,
    // monitor_log_level, mqtt_level. Each sink seeds its own floor where it is
    // built, and the bank raises it from the row it names. A sink floor can only
    // ever be MORE restrictive - an entry the logger dropped never reaches a
    // sink to be admitted - and LOG_CRITICAL clears every floor.

    /** @brief Maximum length of a single log message, before formatting.
     *  Sized against the widest call site (107 bytes). Every %s feed is an
     *  identifier, never free-form text, so that bound is a static property. */
#ifndef BOTANIK_LOG_MESSAGE_LEN
#  define BOTANIK_LOG_MESSAGE_LEN 128
#endif
    BOTANIK_CFG(size_t, LOG_MESSAGE_LEN);

    /** @brief Maximum length of a single formatted log line including metadata
     *  (bytes). Must clear LOG_MESSAGE_LEN plus the prefix format_log_entry()
     *  prepends (timestamp + level + topic + separators, ~40 bytes) plus the
     *  newline a serial writer appends, or a full-length message loses its tail
     *  on the way out. The static_assert below holds the two together. */
#ifndef BOTANIK_LOG_FORMATTED_LEN
#  define BOTANIK_LOG_FORMATTED_LEN 192
#endif
    BOTANIK_CFG(size_t, LOG_FORMATTED_LEN);

    static_assert(LOG_FORMATTED_LEN >= LOG_MESSAGE_LEN + 64,
                  "LOG_FORMATTED_LEN cannot hold LOG_MESSAGE_LEN plus the line prefix");

    /** @brief Maximum number of pointers to registered log writers */
#ifndef BOTANIK_LOG_WRITERS_MAX
#  define BOTANIK_LOG_WRITERS_MAX 8
#endif
    BOTANIK_CFG(uint8_t, LOG_WRITERS_MAX);

    /** @brief Queue depth of a serial sink, and the default a sink takes when
     *  it states none.
     *
     *  The dominant RAM line in a logging build: a writer costs
     *  depth * (LOG_MESSAGE_LEN + 16) + LOG_FORMATTED_LEN. Keep it above the
     *  biggest STORM a run has: the boot roll-call is a line per task, and a bus
     *  going down annunciates a row at a time. A writer drains a few entries per
     *  beat, so the queue is what carries the peak between two of them: at 64
     *  this sink was dropping a dozen entries every boot. */
#ifndef BOTANIK_LOG_QUEUE_SIZE
#  define BOTANIK_LOG_QUEUE_SIZE 96
#endif
    BOTANIK_CFG(size_t, LOG_QUEUE_SIZE);

    /** @brief Queue depth of the LOGS page sink. A page cannot show more than
     *  its scrollback holds, so depth past LOGS_SCROLLBACK_LINES buys burst
     *  absorption between two drains and nothing else. */
#ifndef BOTANIK_LOG_QUEUE_LOGS
#  define BOTANIK_LOG_QUEUE_LOGS 64
#endif
    BOTANIK_CFG(size_t, LOG_QUEUE_LOGS);

    /** @brief Queue depth of the SHELL page sink. Above its scrollback, which
     *  is half this: command output shares the drain with the log tail. */
#ifndef BOTANIK_LOG_QUEUE_SHELL
#  define BOTANIK_LOG_QUEUE_SHELL 48
#endif
    BOTANIK_CFG(size_t, LOG_QUEUE_SHELL);

    /** @brief Queue depth of the HOME page sink. The smallest of them: HOME
     *  shows HOME_SCROLLBACK_LINES rows and keeps no history behind them, and
     *  its write() never yields, so drain() empties this every tick. What is
     *  left is one beat's arrivals plus the critical reserve. */
#ifndef BOTANIK_LOG_QUEUE_HOME
#  define BOTANIK_LOG_QUEUE_HOME 16
#endif
    BOTANIK_CFG(size_t, LOG_QUEUE_HOME);

    // BufferedLogWriter indexes the ring with uint8_t, and the critical reserve
    // has to leave ordinary entries somewhere to sit.
    static_assert(LOG_QUEUE_SIZE  <= 255 && LOG_QUEUE_LOGS <= 255 &&
                  LOG_QUEUE_SHELL <= 255 && LOG_QUEUE_HOME <= 255,
                  "a log queue depth exceeds the writer's uint8_t ring index");

    /** @brief Log writer queue slots reserved for CRITICAL entries: the depth a
     *  storm may not eat into, so the one line that had to survive it does. */
#ifndef BOTANIK_LOG_QUEUE_CRITICAL_RESERVE
#  define BOTANIK_LOG_QUEUE_CRITICAL_RESERVE 8
#endif
    BOTANIK_CFG(size_t, LOG_QUEUE_CRITICAL_RESERVE);

    // The reserve is subtracted from every depth, so the shallowest queue still
    // has to leave an ordinary entry somewhere to sit.
    static_assert(LOG_QUEUE_HOME  > LOG_QUEUE_CRITICAL_RESERVE + 1 &&
                  LOG_QUEUE_SHELL > LOG_QUEUE_CRITICAL_RESERVE + 1 &&
                  LOG_QUEUE_LOGS  > LOG_QUEUE_CRITICAL_RESERVE + 1 &&
                  LOG_QUEUE_SIZE  > LOG_QUEUE_CRITICAL_RESERVE + 1,
                  "a log queue is too shallow for LOG_QUEUE_CRITICAL_RESERVE");

    /** @brief Maximum burst size (number of logs allowed in a quick succession).
     *  Sized to absorb the BOOT burst, which is the biggest one a run ever
     *  has: a line per registered task, on top of the setup() banner, the POST
     *  and the first sweep's quality lines. A site with forty tasks was losing
     *  most of its own roll-call to the limiter. */
#ifndef BOTANIK_LOG_GLOBAL_BUCKET_BURST
#  define BOTANIK_LOG_GLOBAL_BUCKET_BURST 128
#endif
    BOTANIK_CFG(uint16_t, LOG_GLOBAL_BUCKET_BURST);

    /** @brief Refill period in milliseconds (determines the steady-state log rate) */
#ifndef BOTANIK_LOG_GLOBAL_BUCKET_PERIOD_MS
#  define BOTANIK_LOG_GLOBAL_BUCKET_PERIOD_MS 1000
#endif
    BOTANIK_CFG(uint32_t, LOG_GLOBAL_BUCKET_PERIOD_MS);

    /** @brief Maximum burst size per individual topic. BOOT and QUALITY both
     *  spend theirs in the first second, so it is sized for the roll-call the
     *  same way the global bucket is. */
#ifndef BOTANIK_LOG_PERTOPIC_BUCKET_BURST
#  define BOTANIK_LOG_PERTOPIC_BUCKET_BURST 64
#endif
    BOTANIK_CFG(uint16_t, LOG_PERTOPIC_BUCKET_BURST);

    /** @brief Refill period in milliseconds for per-topic buckets */
#ifndef BOTANIK_LOG_PERTOPIC_BUCKET_PERIOD_MS
#  define BOTANIK_LOG_PERTOPIC_BUCKET_PERIOD_MS 5000
#endif
    BOTANIK_CFG(uint32_t, LOG_PERTOPIC_BUCKET_PERIOD_MS);

    /** @brief How often (ms) a summary of suppressed entries may be emitted.
     *  @note Decoupled from the bucket refill periods so a sustained log storm
     *  produces an occasional summary instead of one per refill tick. */
#ifndef BOTANIK_LOG_SUPPRESS_REPORT_PERIOD_MS
#  define BOTANIK_LOG_SUPPRESS_REPORT_PERIOD_MS 10000
#endif
    BOTANIK_CFG(uint32_t, LOG_SUPPRESS_REPORT_PERIOD_MS);

    /** @brief Minimum interval (ms) between dropped-entries-reports.
     *  @note First drop reports instantly; further reports wait out this holdoff. */
#ifndef BOTANIK_LOG_SINK_DROP_REPORT_PERIOD_MS
#  define BOTANIK_LOG_SINK_DROP_REPORT_PERIOD_MS 5000
#endif
    BOTANIK_CFG(uint32_t, LOG_SINK_DROP_REPORT_PERIOD_MS);

    /** @brief Minimum interval (ms) between dropped-bytes-reports.
     *  @note First drop reports instantly; further reports wait this holdoff. */
#ifndef BOTANIK_LOG_TX_DROP_REPORT_PERIOD_MS
#  define BOTANIK_LOG_TX_DROP_REPORT_PERIOD_MS 10000
#endif
    BOTANIK_CFG(uint32_t, LOG_TX_DROP_REPORT_PERIOD_MS);

    /** @} */

} }

#undef BOTANIK_CFG

#endif // LIBBOTANIK_CONFIG_LOGGING_H
