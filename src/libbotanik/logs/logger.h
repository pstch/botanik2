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
        uint16_t    state                   = 0;  ///< tokens currently available
        uint16_t    suppressed              = 0;  ///< entries dropped since the last report
        uint32_t    count                   = 0;  ///< total entries that passed the bucket
    };

    class Logger {
    private:
        using LogEntry_t = PlcIO::LogEntry_t;

        /** @name Global rate-limiter
         *  @{ */
        uint16_t        _global_burst           = Config::LOG_GLOBAL_BUCKET_BURST;
        uint32_t        _global_period_ms       = Config::LOG_GLOBAL_BUCKET_PERIOD_MS;
        uint32_t        _global_fillTime_ms     = 0;
        LogBucket_t     _global_bucket;
        /** @} */

        /** @name Per-topic rate-limiter
         *  @{ */
        uint16_t        _perTopic_burst         = Config::LOG_PERTOPIC_BUCKET_BURST;
        uint32_t        _perTopic_period_ms     = Config::LOG_PERTOPIC_BUCKET_PERIOD_MS;
        uint32_t        _perTopic_fillTime_ms   = 0;
        LogBucket_t     _perTopic_buckets[Config::LOG_TOPIC_MAX];
        /** @} */

        /// Suppressed-message reporting cadence (decoupled from bucket refills).
        uint32_t        _suppress_period_ms     = Config::LOG_SUPPRESS_REPORT_PERIOD_MS;
        uint32_t        _suppress_reportTime_ms = 0;

        /// Registered output writers.
        LogWriter*      _writers[Config::LOG_WRITERS_MAX];
        uint8_t         _writerCount            = 0;
    protected:
        /** @name Rate-limiting internals
         *  @{ */

        /** @brief Try to take a token from @p bucket; returns false if empty. */
        bool _try_bucket(LogBucket_t* bucket);
        /** @brief Add @p amount tokens to @p bucket, clamped to @p burst. */
        void _fill_bucket(LogBucket_t* bucket, uint16_t amount, uint16_t burst);
        /** @brief Emit a synthetic warning entry if @p bucket has suppressed entries. */
        void _report_suppressed(LogBucket_t* bucket, PlcIO::LogTopic topic, uint32_t timestamp_ms);
        /** @brief Build a timestamped entry from a printf-style message and forward it to push_entry(). */
        void _emit(PlcIO::LogLevel level, PlcIO::LogTopic topic, const char* fmt, va_list ap);

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
    };

}}


#endif // LOGS_LOGGER_H
