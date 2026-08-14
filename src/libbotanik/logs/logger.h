// REVIEW_35: TODO
#ifndef LOGS_LOGGER_H
#define LOGS_LOGGER_H

/**
 * @file logger.h
 * @brief Rate-limited log front-end that fans entries out to writers.
 *
 * Logger applies a dual-bucket token-bucket rate limiter (one global bucket,
 * one per topic) before forwarding entries to the registered LogWriters. Call
 * step() once per main-loop period to refill the buckets; use the level
 * helpers (info(), warning(), ...) to emit messages.
 */

#include <cstdarg>                      // va_list
#include <cstdint>                      // uint8_t/uint16_t/uint32_t

#include "libbotanik/config/logging.h"    // Config::LOG_*

#include "libbotanik/core/component.h"  // Component
#include "libbotanik/logs/entry.h"      // Logs::LogEntry
#include "libbotanik/logs/writer.h"     // Logs::LogWriter

namespace Botanik { namespace Tasks { struct Tick; } }

namespace Botanik { namespace Logs {

    /** @brief State of a single token bucket in the rate limiter. */
    struct LogBucket_t {
        uint16_t    state                   = 0;    ///< tokens available
        uint16_t    suppressed              = 0;    ///< dropped since last report
    };

    /** @brief Monotonic loss counter for edge-triggered reporting: first loss
     *  reports instantly, later ones only after a per-category holdoff. */
    struct LossReporter_t {
        uint32_t    reported_count          = 0;    ///< total already reported
        uint32_t    last_report_ms          = 0;    ///< timestamp of last report
        /** gates the instant first report */
        bool        reported                = false;
    };

    /** @class Logger
     *  @brief Rate-limited logging front-end: applies the dual token-bucket
     *         limiter, then fans surviving entries out to the registered
     *         writers. */
    /** @note NOT a Component, deliberately. It would have to be constructed
     *  with the table it registers in, and it passes ITSELF as its own logger -
     *  a self-reference nothing else in the tree needs. It also sits below
     *  everything: `vars/` reports a refused write through one. So the firmware
     *  registers its refill (BotanikFirmware::OwnTasks), as it does for anything
     *  else that is not a Component. */
    class Logger {


        /** @name Rate-limiter
         *  @brief Token buckets and their refill anchors. Burst sizes and
         *  periods come straight from Config.
         *  @{ */

        uint32_t        _global_fillTime_ms     = 0;   ///< last global refill
        LogBucket_t     _global_bucket;
        uint32_t        _perTopic_fillTime_ms   = 0;   ///< last per-topic refill
        /** @brief One bucket per live topic; push_entry folds out-of-range
         *  topics to NONE. */
        LogBucket_t     _perTopic_buckets[Logs::LOG_TOPIC_COUNT];
        uint32_t        _suppress_reportTime_ms = 0;   ///< suppressed-report cadence anchor
        /** @brief Re-entrancy guard for refill(): its suppression and loss
         *  reports LOG, and _emit() refills, so the recursion is real. */
        bool            _refilling = false;

        /** @} */

        /** @name Writers
         *  @{ */

        LogWriter*      _writers[Config::LOG_WRITERS_MAX];
        uint8_t         _writerCount            = 0;

        /** @} */

        /** @name Loss reporting
         *  @{ */

        /** @brief One reporter per writer slot, parallel to _writers: a summed
         *  count could say how many entries were lost but never WHICH sink lost
         *  them, and the sinks differ (MONITOR is a wire, the pages are
         *  screens). Each holds its own holdoff, so a storming sink cannot mask
         *  a quiet one's first drop. */
        LossReporter_t  _sink_loss[Config::LOG_WRITERS_MAX];
        LossReporter_t  _tx_loss;     ///< watched serial TX-full drops
        /** @brief Serials whose TX-full drops we report (console + monitor). */
        Utils::BufferedSerial* _watched_tx[2]  = { nullptr, nullptr };
        uint8_t         _watched_tx_count       = 0;

        /** @brief Minimum level a non-critical entry must reach to be forwarded.
         *
         *  A cache of the `sys_log_level` parameter, which is the setting: it is
         *  read on every _emit() before any formatting, so it is pushed here
         *  rather than looked up. Seeded from Config::LOG_MIN_LEVEL (LOG_DEBUG
         *  on the simulator, LOG_INFO on hardware) for the beats before the
         *  parameter store is read; LOG_CRITICAL is the max, so it never
         *  filters. */
        Logs::LogLevel _min_level = (Logs::LogLevel)Config::LOG_MIN_LEVEL;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Construct a Logger with empty writer list and full rate-limit
         *  buckets */
        Logger();

        /** @} */

        /** @name Writer management
         *  @{ */

        /** @brief Register an output writer; false if the writer list is
         *  full */
        bool add_writer(LogWriter* writer);
        /** @brief True if at least one output writer is registered (POST
         *  check). */
        bool has_writers() const { return _writerCount > 0; }

        /** @brief Set the minimum forwarded level (criticals always pass); see
         *  @c _min_level. */
        void set_min_level(Logs::LogLevel lvl) { _min_level = lvl; }
        /** @brief Current minimum forwarded level. */
        Logs::LogLevel min_level() const { return _min_level; }
        /** @brief Watch a serial so its TX-full drops are reported (the
         *  terminal reserves space, so only non-resuming callers actually
         *  drop). Up to two (console + monitor). */
        void watch_tx(Utils::BufferedSerial* serial) {
            if (serial != nullptr && _watched_tx_count < 2)
                _watched_tx[_watched_tx_count++] = serial;
        }

        /** @} */

        /** @name Dispatch
         *  @{ */

        /** @brief Refill the rate-limiting token buckets, and emit the
         *  edge-triggered report of what got suppressed. Call once per main-loop
         *  period.
         *
         *  NOT Component::step(), and deliberately not named like it: the
         *  Logger is driven with no timestamp and never announced, so a step()
         *  here would hide the base one. It reads the clock itself and refills
         *  by elapsed time, so calling it less often fills more tokens. */
        void refill();
        /** @brief Healthy while at least one writer is registered (a writerless
         *  logger silently swallows everything). */
        bool healthy() const { return has_writers(); }
        /** @brief Forward @p entry to all registered writers after rate-limit
         *  checks */
        void push_entry(const Logs::LogEntry& entry);

        /** @} */

        /**
         * @name Level helpers
         * @brief Build a timestamped Logs::LogEntry with the given topic.
         * @{
         */
        void debug   (Logs::LogTopic topic, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
        void info    (Logs::LogTopic topic, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
        void notice  (Logs::LogTopic topic, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
        void warning (Logs::LogTopic topic, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
        void error   (Logs::LogTopic topic, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
        void critical(Logs::LogTopic topic, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
        /** @} */

    protected:
        /** @name Rate-limiting internals
         *  @{ */

        /** @brief Try to take a token from @p bucket; returns false if
         *  empty. */
        bool _try_bucket(LogBucket_t* bucket);
        /** @brief Add @p amount tokens to @p bucket, clamped to @p burst. */
        void _fill_bucket(LogBucket_t* bucket, uint16_t amount, uint16_t burst);
        /** @brief Push a CRITICAL entry straight to the writers (bypassing the
         *  rate limiter) so loss/suppression reports survive the storm they
         *  describe. */
        void _emit_critical(Logs::LogTopic topic, uint32_t timestamp_ms,
                            const char* fmt, ...);
        /** @brief Emit a synthetic warning if @p bucket has suppressed
         *  entries. */
        void _report_suppressed(LogBucket_t* bucket, Logs::LogTopic topic,
                                uint32_t timestamp_ms);
        /** @brief Edge-triggered loss report: emit a critical "<delta> <what>"
         *  entry when @p total has grown and the holdoff elapsed (or on the
         *  first loss). Pushed straight to the writers (bypassing the rate
         *  limiter), as LOG_CRITICAL so the writers never drop it.
         *  @param who names the source ("<who>: <delta> <what>"); null omits it,
         *  which is what an unnamed sink and the TX report both give. */
        void _report_loss(LossReporter_t& rep, uint32_t total, uint32_t interval_ms,
                          uint32_t now_ms, const char* what, const char* who);
        /** @brief Build a timestamped entry from a printf-style message and
         *  forward it to push_entry(). */
        void _emit(Logs::LogLevel level, Logs::LogTopic topic, const char* fmt,
                   va_list ap);
        /** @brief Stamp @p e (assumed zero-inited) with level/topic/timestamp and
         *  the formatted message, clamping the length. The shared body of _emit
         *  and _emit_critical. */
        void _build_entry(Logs::LogEntry& e, Logs::LogLevel level, Logs::LogTopic topic,
                          uint32_t timestamp_ms, const char* fmt, va_list ap);

        /** @} */
    };

}}


#endif // LOGS_LOGGER_H
