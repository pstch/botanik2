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

    /** @name Per-sink floors
     *
     *  What each sink shows, on top of LOG_MIN_LEVEL. These are the FACTORY
     *  values: each is behind a parameter an operator can move
     *  (console_home_level, console_shell_level, console_logs_level,
     *  monitor_level, mqtt_level), so what a unit shows where is a property of
     *  the installation rather than of the build. A sink floor can
     *  only ever be MORE restrictive: an entry the logger dropped never reaches
     *  a sink to be admitted. LOG_CRITICAL is the enum maximum, so a critical
     *  clears every floor.
     *
     *  HOME is a landing screen, so it carries only what is worth looking up
     *  for; SHELL keeps the running commentary; LOGS is the record.
     *  @{ */

    /** @brief HOME page log band. */
#ifndef BOTANIK_HOME_LOG_MIN_LEVEL
#  define BOTANIK_HOME_LOG_MIN_LEVEL 4   /* Logs::LOG_NOTICE */
#endif
    BOTANIK_CFG(uint8_t, HOME_LOG_MIN_LEVEL);

    /** @brief SHELL page log drain. */
#ifndef BOTANIK_SHELL_LOG_MIN_LEVEL
#  define BOTANIK_SHELL_LOG_MIN_LEVEL 2   /* Logs::LOG_INFO */
#endif
    BOTANIK_CFG(uint8_t, SHELL_LOG_MIN_LEVEL);

    /** @brief LOGS page. Everything the logger forwarded. */
#ifndef BOTANIK_LOGS_LOG_MIN_LEVEL
#  define BOTANIK_LOGS_LOG_MIN_LEVEL 1   /* Logs::LOG_DEBUG */
#endif
    BOTANIK_CFG(uint8_t, LOGS_LOG_MIN_LEVEL);

    /** @brief The monitor port. Open, like the LOGS page: the port carries the
     *  log and nothing else, and it is where a session is captured whole. */
#ifndef BOTANIK_MONITOR_LOG_MIN_LEVEL
#  define BOTANIK_MONITOR_LOG_MIN_LEVEL 1   /* Logs::LOG_DEBUG */
#endif
    BOTANIK_CFG(uint8_t, MONITOR_LOG_MIN_LEVEL);

    /** @} */

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
     *  build, but keep it above the boot burst (~20 entries in the first beat)
     *  or the announces are what gets dropped. */
#ifndef BOTANIK_LOG_QUEUE_SIZE
#  define BOTANIK_LOG_QUEUE_SIZE 64
#endif
    BOTANIK_CFG(size_t, LOG_QUEUE_SIZE);

    // BufferedLogWriter indexes the ring with uint8_t.
    static_assert(LOG_QUEUE_SIZE <= 255, "LOG_QUEUE_SIZE exceeds the writer's uint8_t ring index");

    /** @brief Log writer queue slots reserved for CRITICAL entries. */
#ifndef BOTANIK_LOG_QUEUE_CRITICAL_RESERVE
#  define BOTANIK_LOG_QUEUE_CRITICAL_RESERVE 4
#endif
    BOTANIK_CFG(size_t, LOG_QUEUE_CRITICAL_RESERVE);

    /** @brief Maximum burst size (number of logs allowed in a quick succession).
     *  Sized to absorb the boot burst: every Component announces on its first
     *  step, all in the first beat, on top of the setup() banner + POST. */
#ifndef BOTANIK_LOG_GLOBAL_BUCKET_BURST
#  define BOTANIK_LOG_GLOBAL_BUCKET_BURST 32
#endif
    BOTANIK_CFG(uint16_t, LOG_GLOBAL_BUCKET_BURST);

    /** @brief Refill period in milliseconds (determines the steady-state log rate) */
#ifndef BOTANIK_LOG_GLOBAL_BUCKET_PERIOD_MS
#  define BOTANIK_LOG_GLOBAL_BUCKET_PERIOD_MS 1000
#endif
    BOTANIK_CFG(uint32_t, LOG_GLOBAL_BUCKET_PERIOD_MS);

    /** @brief Maximum burst size per individual topic */
#ifndef BOTANIK_LOG_PERTOPIC_BUCKET_BURST
#  define BOTANIK_LOG_PERTOPIC_BUCKET_BURST 32
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
