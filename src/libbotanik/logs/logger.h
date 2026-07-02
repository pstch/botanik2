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

#include <cstdint>
#include <cstdarg>

#include "libbotanik/plcio/vars.h"
#include "libbotanik/logs/writer.h"
#include "libbotanik_config.h"

namespace Botanik { namespace Logs {

    /** @brief State of a single token bucket in the rate limiter. */
    struct LogBucket_t {
        uint16_t    state                   = 0;    ///< tokens available
        uint16_t    suppressed              = 0;    ///< dropped since last report
        uint32_t    count                   = 0;    ///< total entries passed
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
    class Logger {
    private:
        using LogEntry_t = PlcIO::LogEntry_t;

        /** @name Global rate-limiter
         *  @{ */

        /** max tokens */
        uint16_t        _global_burst           = Config::LOG_GLOBAL_BUCKET_BURST;
        /** refill period */
        uint32_t        _global_period_ms       = Config::LOG_GLOBAL_BUCKET_PERIOD_MS;
        uint32_t        _global_fillTime_ms     = 0;   ///< last refill time
        LogBucket_t     _global_bucket;

        /** @} */

        /** @name Per-topic rate-limiter
         *  @{ */

        uint16_t        _perTopic_burst         = Config::LOG_PERTOPIC_BUCKET_BURST;
        uint32_t        _perTopic_period_ms     = Config::LOG_PERTOPIC_BUCKET_PERIOD_MS;
        uint32_t        _perTopic_fillTime_ms   = 0;
        /** @brief Per-topic buckets, one per live topic. push_entry clamps any index to
         *  LOG_TOPIC_COUNT (out-of-range topics fold to NONE), so this is the full range used. */
        LogBucket_t     _perTopic_buckets[Logs::LOG_TOPIC_COUNT];

        /** @} */

        /** @name Suppression reporting
         *  @{ */

        /** @brief Suppressed-report cadence (ms), decoupled from bucket
         *  refills. */
        uint32_t        _suppress_period_ms     = Config::LOG_SUPPRESS_REPORT_PERIOD_MS;
        uint32_t        _suppress_reportTime_ms = 0;

        /** @} */

        /** @name Writers
         *  @{ */

        LogWriter*      _writers[Config::LOG_WRITERS_MAX];
        uint8_t         _writerCount            = 0;

        /** @} */

        /** @name Loss reporting
         *  @{ */

        LossReporter_t  _sink_loss;   ///< entries dropped at the sinks
        LossReporter_t  _tx_loss;     ///< watched serial TX-full drops
        /** @brief Serials whose TX-full drops we report (console + monitor). */
        Utils::BufferedSerial* _watched_tx[2]  = { nullptr, nullptr };
        uint8_t         _watched_tx_count       = 0;

        /** @brief Minimum level a non-critical entry must reach to be forwarded
         *  (set by the `loglevel` command). Defaults from Config::LOG_MIN_LEVEL
         *  (LOG_DEBUG on the simulator, LOG_INFO on hardware); LOG_CRITICAL is
         *  the max, so it never filters. */
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
        /** @brief Unregister a previously added writer; returns false if not
         *  found */
        bool remove_writer(LogWriter* writer);
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

        /** @brief Refill rate-limiting token buckets (call once per main-loop
         *  period) */
        void step();
        /** @brief Forward @p entry to all registered writers after rate-limit
         *  checks */
        void push_entry(const LogEntry_t& entry);

        /** @} */

        /**
         * @name Level helpers
         * @brief Build a LogEntry_t (timestamped with HAL::Clock::current_ms()) at the
         *        given @p topic from a printf-style message and forward it
         *        through push_entry(). One helper per LogLevel.
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
         *  limiter), as LOG_CRITICAL so the writers never drop it. */
        void _report_loss(LossReporter_t& rep, uint32_t total, uint32_t interval_ms,
                          uint32_t now_ms, const char* what);
        /** @brief Build a timestamped entry from a printf-style message and
         *  forward it to push_entry(). */
        void _emit(Logs::LogLevel level, Logs::LogTopic topic, const char* fmt,
                   va_list ap);

        /** @} */
    };

}}


#endif // LOGS_LOGGER_H
