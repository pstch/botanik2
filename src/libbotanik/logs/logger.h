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
        /** @brief Tokens currently available. */
        uint16_t    state                   = 0;
        /** @brief Entries dropped since the last report. */
        uint16_t    suppressed              = 0;
        /** @brief Total entries that passed the bucket. */
        uint32_t    count                   = 0;
    };

    /** @brief Tracks a monotonic loss counter for edge-triggered reporting: the
     *  first loss reports instantly, later ones only after a per-category holdoff. */
    struct LossReporter_t {
        /** @brief Loss total already reported. */
        uint32_t    reported_count          = 0;
        /** @brief Time of the last report (ms). */
        uint32_t    last_report_ms          = 0;
        /** @brief Whether a first report has been emitted (gates the instant report). */
        bool        reported                = false;
    };

    /** @class Logger
     *  @brief Rate-limited logging front-end: applies the dual token-bucket limiter,
     *         then fans surviving entries out to the registered writers.
     */
    class Logger {
    private:
        using LogEntry_t = PlcIO::LogEntry_t;
 
        /** @name Global rate-limiter
         *  @{ */

        /** @brief Burst capacity (max tokens) of the global bucket. */
        uint16_t        _global_burst           = Config::LOG_GLOBAL_BUCKET_BURST;
        /** @brief Refill period of the global bucket (ms). */
        uint32_t        _global_period_ms       = Config::LOG_GLOBAL_BUCKET_PERIOD_MS;
        /** @brief Wall-clock time of the last global-bucket refill. */
        uint32_t        _global_fillTime_ms     = 0;
        /** @brief The single global token bucket. */
        LogBucket_t     _global_bucket;

        /** @} */

        /** @name Per-topic rate-limiter
         *  @{ */

        /** @brief Burst capacity (max tokens) of each per-topic bucket. */
        uint16_t        _perTopic_burst         = Config::LOG_PERTOPIC_BUCKET_BURST;
        /** @brief Refill period of the per-topic buckets (ms). */
        uint32_t        _perTopic_period_ms     = Config::LOG_PERTOPIC_BUCKET_PERIOD_MS;
        /** @brief Wall-clock time of the last per-topic refill. */
        uint32_t        _perTopic_fillTime_ms   = 0;
        /** @brief Per-topic token buckets. Sized by LOG_TOPIC_MAX (the full wire
         *  range) so the footprint stays fixed as topics are added; only the first
         *  LOG_TOPIC_COUNT entries (the defined topics) are ever filled or scanned. */
        LogBucket_t     _perTopic_buckets[Config::LOG_TOPIC_MAX];

        /** @} */

        /** @name Suppression reporting
         *  @{ */

        /** @brief Cadence for suppressed-message reports (ms), decoupled from
         *  bucket refills. */
        uint32_t        _suppress_period_ms     = Config::LOG_SUPPRESS_REPORT_PERIOD_MS;
        /** @brief Wall-clock time of the last suppression report. */
        uint32_t        _suppress_reportTime_ms = 0;

        /** @} */

        /** @name Writers
         *  @{ */

        /** @brief Registered output writers. */
        LogWriter*      _writers[Config::LOG_WRITERS_MAX];
        /** @brief Number of registered writers. */
        uint8_t         _writerCount            = 0;

        /** @} */

        /** @name Loss reporting
         *  @{ */

        /** @brief Edge-triggered report state for entries dropped at the sinks. */
        LossReporter_t  _sink_loss;
        /** @brief Edge-triggered report state for watched serial TX-full drops. */
        LossReporter_t  _tx_loss;
        /** @brief Serials whose TX-full drops we report (console + monitor). */
        Utils::BufferedSerial* _watched_tx[2]  = { nullptr, nullptr };
        /** @brief Number of registered TX watches. */
        uint8_t         _watched_tx_count       = 0;

        /** @brief Minimum level a non-critical entry must reach to be forwarded (set at
         *  runtime by the `loglevel` shell command). Defaults to LOG_DEBUG (pass all) in
         *  DEBUG builds and LOG_INFO in release, so debug spew is filtered on hardware by
         *  default; LOG_CRITICAL is the max value so a critical is never filtered. */
        PlcIO::LogLevel _min_level =
#ifdef DEBUG
            PlcIO::LOG_DEBUG;
#else
            PlcIO::LOG_INFO;
#endif

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Construct a Logger with empty writer list and full rate-limit buckets */
        Logger();

        /** @} */

        /** @name Writer management
         *  @{ */

        /** @brief Register an output writer; returns false if the writer list is full */
        bool add_writer(LogWriter* writer);
        /** @brief Unregister a previously added writer; returns false if not found */
        bool remove_writer(LogWriter* writer);
        /** @brief True if at least one output writer is registered (POST check). */
        bool has_writers() const { return _writerCount > 0; }

        /** @brief Set the minimum forwarded level (criticals always pass); see @c _min_level. */
        void set_min_level(PlcIO::LogLevel lvl) { _min_level = lvl; }
        /** @brief Current minimum forwarded level. */
        PlcIO::LogLevel min_level() const { return _min_level; }
        /** @brief Watch a serial so its TX-full drops are reported (the terminal reserves
         *  space, so only non-resuming callers actually drop). Up to two (console + monitor). */
        void watch_tx(Utils::BufferedSerial* serial) {
            if (serial != nullptr && _watched_tx_count < 2) _watched_tx[_watched_tx_count++] = serial;
        }

        /** @} */

        /** @name Dispatch
         *  @{ */

        /** @brief Refill rate-limiting token buckets (call once per main-loop period) */
        void step();
        /** @brief Forward @p entry to all registered writers after rate-limit checks */
        void push_entry(const LogEntry_t& entry);

        /** @} */

        /**
         * @name Level helpers
         * @brief Build a LogEntry_t (timestamped with HAL::current_ms()) at the
         *        given @p topic from a printf-style message and forward it through
         *        push_entry(). One helper per LogLevel.
         * @{
         */
        void debug   (PlcIO::LogTopic topic, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
        void info    (PlcIO::LogTopic topic, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
        void notice  (PlcIO::LogTopic topic, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
        void warning (PlcIO::LogTopic topic, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
        void error   (PlcIO::LogTopic topic, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
        void critical(PlcIO::LogTopic topic, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
        /** @} */

    protected:
        /** @name Rate-limiting internals
         *  @{ */

        /** @brief Try to take a token from @p bucket; returns false if empty. */
        bool _try_bucket(LogBucket_t* bucket);
        /** @brief Add @p amount tokens to @p bucket, clamped to @p burst. */
        void _fill_bucket(LogBucket_t* bucket, uint16_t amount, uint16_t burst);
        /** @brief Emit a synthetic warning entry if @p bucket has suppressed entries. */
        void _report_suppressed(LogBucket_t* bucket, PlcIO::LogTopic topic, uint32_t timestamp_ms);
        /** @brief Edge-triggered loss report: emit a critical "<delta> <what>" entry
         *  when @p total has grown and the holdoff elapsed (or on the first loss).
         *  Pushed straight to the writers (bypassing the rate limiter), as
         *  LOG_CRITICAL so the writers never drop it. */
        void _report_loss(LossReporter_t& rep, uint32_t total, uint32_t interval_ms,
                          uint32_t now_ms, const char* what);
        /** @brief Build a timestamped entry from a printf-style message and forward it to push_entry(). */
        void _emit(PlcIO::LogLevel level, PlcIO::LogTopic topic, const char* fmt, va_list ap);

        /** @} */
    };

}}


#endif // LOGS_LOGGER_H
