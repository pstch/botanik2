/**
 * @file logger.cpp
 * @brief Logger implementation — token-bucket refill and dispatch.
 *
 * step() refills the global and per-topic buckets on their own fast cadences
 * (integer division of elapsed time by the bucket period, advancing the fill
 * anchor by whole periods only), and drains the suppressed-message counters on
 * a separate slower cadence so a sustained storm yields an occasional summary
 * rather than one report per refill. push_entry() applies the per-topic filter
 * before the global one, so per-topic-suppressed messages don't drain the
 * global bucket.
 */
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
        if (bucket->state == 0) {
            // Saturate rather than wrap: the suppressed count is now drained on a
            // slower cadence, so a long storm could otherwise overflow a uint16_t.
            if (bucket->suppressed != UINT16_MAX) bucket->suppressed++;
            return false;
        }
        else { bucket->count++; bucket->state--; return true; }
    }

    void Logger::_fill_bucket(LogBucket_t* bucket, uint16_t amount, uint16_t burst) {
        if (amount > 0 && bucket-> state < burst) {
            bucket->state = std::min((uint16_t)(bucket->state + amount), burst);
        }
    }

    Logger::Logger() {
        _perTopic_fillTime_ms = HAL::current_ms();
        _global_fillTime_ms = HAL::current_ms();
        _suppress_reportTime_ms = HAL::current_ms();
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
        int w = snprintf(
            entry.message, sizeof(entry.message),
            "%u msgs suppressed", (unsigned)bucket->suppressed);
        if (w < 0) w = 0;
        entry.length = (uint16_t)std::min((size_t)w, sizeof(entry.message) - 1);
        for (uint8_t i = 0; i < _writerCount; i++) _writers[i]->push(&entry);
        bucket->suppressed = 0;
    }

    void Logger::step() {
        uint32_t now_ms         = HAL::current_ms();
        // Refill the token buckets on their own (fast) cadences.
        uint32_t global_amount  = (now_ms - _global_fillTime_ms) / _global_period_ms;
        _global_fillTime_ms    += global_amount * _global_period_ms;
        _fill_bucket(&_global_bucket, global_amount, _global_burst);
        uint32_t topic_amount   = (now_ms - _perTopic_fillTime_ms) / _perTopic_period_ms;
        _perTopic_fillTime_ms  += topic_amount * _perTopic_period_ms;
        for (uint8_t i = 0; i < Config::LOG_TOPIC_MAX; ++i)
            _fill_bucket(&_perTopic_buckets[i], topic_amount, _perTopic_burst);
        // Drain suppressed-message counters on a separate, slower cadence so a
        // sustained storm yields an occasional summary rather than one per refill.
        uint32_t report_amount  = (now_ms - _suppress_reportTime_ms) / _suppress_period_ms;
        if (report_amount > 0) {
            _suppress_reportTime_ms += report_amount * _suppress_period_ms;
            _report_suppressed(&_global_bucket, LogTopic::NONE, now_ms);
            for (uint8_t i = 0; i < Config::LOG_TOPIC_MAX; ++i)
                _report_suppressed(&_perTopic_buckets[i], (LogTopic)i, now_ms);
        }
    }

    void Logger::_emit(PlcIO::LogLevel level, PlcIO::LogTopic topic, const char* fmt, va_list ap) {
        LogEntry_t entry = {};
        entry.timestamp = HAL::current_ms();
        entry.level     = level;
        entry.topic     = topic;
        int w = vsnprintf(entry.message, sizeof(entry.message), fmt, ap);
        if (w < 0) w = 0;
        entry.length = (uint16_t)std::min((size_t)w, sizeof(entry.message) - 1);
        push_entry(entry);
    }

    void Logger::debug(PlcIO::LogTopic topic, const char* fmt, ...) {
        va_list ap; va_start(ap, fmt); _emit(PlcIO::LOG_DEBUG, topic, fmt, ap); va_end(ap);
    }
    void Logger::info(PlcIO::LogTopic topic, const char* fmt, ...) {
        va_list ap; va_start(ap, fmt); _emit(PlcIO::LOG_INFO, topic, fmt, ap); va_end(ap);
    }
    void Logger::notice(PlcIO::LogTopic topic, const char* fmt, ...) {
        va_list ap; va_start(ap, fmt); _emit(PlcIO::LOG_NOTICE, topic, fmt, ap); va_end(ap);
    }
    void Logger::warning(PlcIO::LogTopic topic, const char* fmt, ...) {
        va_list ap; va_start(ap, fmt); _emit(PlcIO::LOG_WARNING, topic, fmt, ap); va_end(ap);
    }
    void Logger::error(PlcIO::LogTopic topic, const char* fmt, ...) {
        va_list ap; va_start(ap, fmt); _emit(PlcIO::LOG_ERROR, topic, fmt, ap); va_end(ap);
    }
    void Logger::critical(PlcIO::LogTopic topic, const char* fmt, ...) {
        va_list ap; va_start(ap, fmt); _emit(PlcIO::LOG_CRITICAL, topic, fmt, ap); va_end(ap);
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