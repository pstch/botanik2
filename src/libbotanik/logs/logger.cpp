#include <cstdio>

#include "libbotanik/hal/platform.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/utils/time.h"

namespace Botanik { namespace Logs {

    namespace {
        using LogEntry_t = Botanik::PlcIO::LogEntry_t;
        using LogTopic = Botanik::PlcIO::LogTopic;
    }

    bool Logger::_try_bucket(LogBucket_t* bucket) {
        if (bucket->state <= 0) { bucket->suppressed++;             return false; }
        else                    { bucket->count++; bucket->state--; return true; }
    }

    void Logger::_fill_bucket(LogBucket_t* bucket, uint16_t amount, uint16_t burst) {
        if (amount > 0 && bucket-> state < burst) {
            bucket->state = std::min((uint16_t)(bucket->state + amount), burst);
        }
    }

    Logger::Logger() {
        _perTopic_fillTime_ms = HAL::current_ms();
        _global_fillTime_ms = HAL::current_ms();
        _global_bucket.state = _global_burst;
        for (uint8_t i = 0; i < Config::LOG_TOPIC_MAX; ++i) {
            _perTopic_buckets[i].state = _perTopic_burst;
        }
    }

    bool Logger::add_writer(LogWriter* writer) {
        if (writer == nullptr || _writerCount >= Config::LOG_WRITERS_MAX) return false;
        for (uint8_t i = 0; i < _writerCount; i++) if (_writers[i] == writer) return true;
        _writers[_writerCount++] = writer;
        return true;
    }

    bool Logger::remove_writer(LogWriter* writer) {
        if (writer == nullptr || _writerCount == 0) return false;
        uint8_t i = 0;
        while (i < _writerCount && _writers[i] != writer) i++;
        if (i == _writerCount) return false;
        for (uint8_t j = i; j < _writerCount - 1; ++j) _writers[j] = _writers[j + 1];
        _writers[--_writerCount] = nullptr;
        return true;
    }

    void Logger::_report_suppressed(LogBucket_t* bucket, PlcIO::LogTopic topic, uint32_t timestamp_ms) {
        if (bucket->suppressed == 0) return;
        LogEntry_t entry = {};
        entry.timestamp = timestamp_ms;
        entry.level     = PlcIO::LOG_WARNING;
        entry.topic     = topic;
        entry.length    = (uint16_t)snprintf(
            entry.message, sizeof(entry.message),
            "%u msgs suppressed", (unsigned)bucket->suppressed);
        for (uint8_t i = 0; i < _writerCount; i++) _writers[i]->push(&entry);
        bucket->suppressed = 0;
    }

    void Logger::step() {
        uint32_t now_ms         = HAL::current_ms();
        uint32_t global_amount  = (now_ms - _global_fillTime_ms) / _global_period_ms;
        _global_fillTime_ms    += global_amount * _global_period_ms;
        _fill_bucket(&_global_bucket, global_amount, _global_burst);
        if (global_amount > 0)
            _report_suppressed(&_global_bucket, LogTopic::NONE, now_ms);
        uint32_t topic_amount   = (now_ms - _perTopic_fillTime_ms) / _perTopic_period_ms;
        _perTopic_fillTime_ms  += topic_amount * _perTopic_period_ms;
        for (uint8_t i = 0; i < Config::LOG_TOPIC_MAX; ++i)
            _fill_bucket(&_perTopic_buckets[i], topic_amount, _perTopic_burst);
        if (topic_amount > 0)
            for (uint8_t i = 0; i < Config::LOG_TOPIC_MAX; ++i)
                _report_suppressed(&_perTopic_buckets[i], (LogTopic)i, now_ms);
    }

    void Logger::push_entry(const PlcIO::LogEntry_t& entry) {
        PlcIO::LogTopic topic = entry.topic >=
            Config::LOG_TOPIC_MAX ? LogTopic::NONE : entry.topic;                       // determine the topic to use for this entry
        LogBucket_t* topic_bucket = &_perTopic_buckets[topic];                          // get the bucket for this topic
        if (topic == LogTopic::NONE || _try_bucket(topic_bucket))                         // apply the per-topic filter first so global isn't drained by per-topic-suppressed msgs
            if (_try_bucket(&_global_bucket))                                           // then apply the global filter
                for (uint8_t i = 0; i < _writerCount; i++)                              // then, for each registered writer
                    _writers[i]->push(&entry);                                          // push the entry to the writer's queue
    }


}}