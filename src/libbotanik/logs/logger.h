#ifndef LOGS_LOGGER_H
#define LOGS_LOGGER_H

#include <cstdint>
#include <cstdarg>

#include "libbotanik/plcio/vars.h"
#include "libbotanik/logs/writer.h"
#include "libbotanik_config.h"

namespace Botanik { namespace Logs {

    struct LogBucket_t {
        uint16_t    state                   = 0;
        uint16_t    suppressed              = 0;
        uint32_t    count                   = 0;
    };

    class Logger {
    private:
        using LogEntry_t = PlcIO::LogEntry_t;
        // global rate-limiter
        uint16_t        _global_burst           = Config::LOG_GLOBAL_BUCKET_BURST;
        uint32_t        _global_period_ms       = Config::LOG_GLOBAL_BUCKET_PERIOD_MS;
        uint32_t        _global_fillTime_ms     = 0;
        LogBucket_t     _global_bucket;
        // per-topic rate-limiter
        uint16_t        _perTopic_burst         = Config::LOG_PERTOPIC_BUCKET_BURST;
        uint32_t        _perTopic_period_ms     = Config::LOG_PERTOPIC_BUCKET_PERIOD_MS;
        uint32_t        _perTopic_fillTime_ms   = 0;
        LogBucket_t     _perTopic_buckets[Config::LOG_TOPIC_MAX];
        // suppressed-message reporting cadence (decoupled from bucket refills)
        uint32_t        _suppress_period_ms     = Config::LOG_SUPPRESS_REPORT_PERIOD_MS;
        uint32_t        _suppress_reportTime_ms = 0;
        // output writers list
        LogWriter*      _writers[Config::LOG_WRITERS_MAX];
        uint8_t         _writerCount            = 0;
    protected:
        // try to take a token from a bucket
        bool _try_bucket(LogBucket_t* bucket);
        // try to fill a bucket with the given amount of tokens
        void _fill_bucket(LogBucket_t* bucket, uint16_t amount, uint16_t burst);
        // emit a synthetic warning entry if the bucket has suppressed entries
        void _report_suppressed(LogBucket_t* bucket, PlcIO::LogTopic topic, uint32_t timestamp_ms);
        // build a timestamped entry from a printf-style message and forward it to push_entry()
        void _emit(PlcIO::LogLevel level, PlcIO::LogTopic topic, const char* fmt, va_list ap);
    public:
        /** @brief Construct a Logger with empty writer list and full rate-limit buckets */
        Logger();
        /** @brief Register an output writer; returns false if the writer list is full */
        bool add_writer(LogWriter* writer);
        /** @brief Unregister a previously added writer; returns false if not found */
        bool remove_writer(LogWriter* writer);
        /** @brief Refill rate-limiting token buckets (call once per main-loop period) */
        void step();
        /** @brief Forward @p entry to all registered writers after rate-limit checks */
        void push_entry(const LogEntry_t& entry);

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