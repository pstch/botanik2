#ifndef LOGS_LOGGER_H
#define LOGS_LOGGER_H

#include <cstdint>

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
        uint16_t        _global_period_ms       = Config::LOG_GLOBAL_BUCKET_PERIOD_MS;
        uint32_t        _global_fillTime_ms     = 0;
        LogBucket_t     _global_bucket;
        // per-topic rate-limiter
        uint16_t        _perTopic_burst         = Config::LOG_PERTOPIC_BUCKET_BURST;
        uint16_t        _perTopic_period_ms     = Config::LOG_PERTOPIC_BUCKET_PERIOD_MS;
        uint32_t        _perTopic_fillTime_ms   = 0;
        LogBucket_t     _perTopic_buckets[Config::LOG_TOPIC_MAX];
        // output writers list
        LogWriter*      _writers[Config::LOG_WRITERS_MAX];
        uint8_t         _writerCount            = 0;
    protected:
        // try to take a token from a bucket
        bool _try_bucket(LogBucket_t* bucket);
        // try to fill a bucket with the given amount of tokens
        void _fill_bucket(LogBucket_t* bucket, uint16_t amount, uint16_t burst);
    public:
        // Constructor
        Logger();
        // Add a writer
        bool add_writer(LogWriter* writer);
        // Remove a writer
        bool remove_writer(LogWriter* writer);
        // Fill rate-limiting buckets
        void fill_buckets();
        // Push a new log entry to the writers, after applying rate-limiting
        void push_entry(const LogEntry_t entry);
    };
    
}}


#endif LOGS_LOGGER_H