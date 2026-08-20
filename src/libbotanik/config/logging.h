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

    /** @brief The FACTORY DEFAULT for the `sys_log_level` parameter, which is
     *  the master floor: below it nothing is forwarded to any sink.
     *
     *  The live value is the parameter, so a level set over the terminal
     *  survives a reboot. DEBUG, so the LOGS page can show everything; each page
     *  sink then applies its own floor on top (below). Cheap to leave open:
     *  every debug() call site is a one-shot boot line. */
#ifndef BOTANIK_LOG_MIN_LEVEL
#  define BOTANIK_LOG_MIN_LEVEL 1   /* Logs::LOG_DEBUG */
#endif
    BOTANIK_CFG(uint8_t, LOG_MIN_LEVEL);

    // What each sink shows on top of LOG_MIN_LEVEL is a PARAMETER, not a macro:
    // console_home_level, console_shell_level, console_logs_level,
    // monitor_level, mqtt_level. Each sink seeds its own floor where it is
    // built, and the bank raises it from the row it names. A sink floor can only
    // ever be MORE restrictive - an entry the logger dropped never reaches a
    // sink to be admitted - and LOG_CRITICAL clears every floor.

    /** @brief Maximum length of a single log message, before formatting. */
#ifndef BOTANIK_LOG_MESSAGE_LEN
#  define BOTANIK_LOG_MESSAGE_LEN 256
#endif
    BOTANIK_CFG(size_t, LOG_MESSAGE_LEN);

    /** @brief Maximum length of a single formatted log line including metadata
     *  (bytes). Must clear LOG_MESSAGE_LEN plus the prefix format_log_entry()
     *  prepends (timestamp + level + topic + separators, ~40 bytes) plus the
     *  newline a serial writer appends, or a full-length message loses its tail
     *  on the way out. The static_assert below holds the two together. */
#ifndef BOTANIK_LOG_FORMATTED_LEN
#  define BOTANIK_LOG_FORMATTED_LEN 320
#endif
    BOTANIK_CFG(size_t, LOG_FORMATTED_LEN);

    static_assert(LOG_FORMATTED_LEN >= LOG_MESSAGE_LEN + 64,
                  "LOG_FORMATTED_LEN cannot hold LOG_MESSAGE_LEN plus the line prefix");

    /** @brief Maximum number of pointers to registered log writers */
#ifndef BOTANIK_LOG_WRITERS_MAX
#  define BOTANIK_LOG_WRITERS_MAX 8
#endif
    BOTANIK_CFG(uint8_t, LOG_WRITERS_MAX);

    /** @brief Per-writer in-flight message queue depth.
     *
     *  The dominant RAM line in a logging build: one writer costs roughly
     *  LOG_QUEUE_SIZE * (LOG_MESSAGE_LEN + 16) + LOG_FORMATTED_LEN, and the full
     *  page set registers five. Lowering it is the first thing to try on a tight
     *  build, but keep it above the biggest STORM a run has: the boot roll-call
     *  is a line per task, and a bus going down annunciates a row at a time.
     *  A writer drains a few entries per beat, so the queue is what carries the
     *  peak between two of them - at 64 the sink was dropping a dozen entries
     *  every boot. Half again buys that headroom and stays inside the 32 KB a
     *  writer is budgeted (tests/test_site.cpp): each slot carries a whole
     *  LOG_MESSAGE_LEN, so this line is 26 KB per writer and there are five. */
#ifndef BOTANIK_LOG_QUEUE_SIZE
#  define BOTANIK_LOG_QUEUE_SIZE 96
#endif
    BOTANIK_CFG(size_t, LOG_QUEUE_SIZE);

    // BufferedLogWriter indexes the ring with uint8_t.
    static_assert(LOG_QUEUE_SIZE <= 255, "LOG_QUEUE_SIZE exceeds the writer's uint8_t ring index");

    /** @brief Log writer queue slots reserved for CRITICAL entries: the depth a
     *  storm may not eat into, so the one line that had to survive it does. */
#ifndef BOTANIK_LOG_QUEUE_CRITICAL_RESERVE
#  define BOTANIK_LOG_QUEUE_CRITICAL_RESERVE 8
#endif
    BOTANIK_CFG(size_t, LOG_QUEUE_CRITICAL_RESERVE);

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
