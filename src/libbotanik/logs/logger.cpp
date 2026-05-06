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
        if (writer == nullptr || _writerCount >= Config::LOG_WRITERS_MAX) return false;    // bail out if given a nullpointer or if the writer list is full
        for (uint8_t i = 0; i < _writerCount; i++) if (_writers[i] == writer) return true; // check for duplicates
        _writers[_writerCount++] = writer; return true;                            // store the pointer and increment writer count
    }

    bool Logger::remove_writer(LogWriter* writer) {
        // do some checks and find the writer's current index
        if (writer == nullptr || _writerCount == 0) return false;                   // bail out if given a nullpointer of if we have no writers
        uint8_t i = 0; while (i < _writerCount && _writers[i] != writer) i++;       // find the index of the given writer
        if (i == _writerCount) return false;                                        // bail out if no writer was found
        for (uint8_t j = i; j < _writerCount - 1; ++j) _writers[j] = _writers[j+1]; // shift subsequent writers left to fill the gap
        _writers[--_writerCount] = nullptr; return true;                            // clear the now-vacant last slot
    }

    void Logger::fill_buckets() {
        // XXX: the choice of timer method here determines whether we want to buckets to fill in SLEEP/STOP or not.
        uint32_t _time_ms = HAL::current_ms();
        uint32_t _global_amount = (_time_ms - _global_fillTime_ms) / _global_period_ms;
        _global_fillTime_ms += _global_amount * _global_period_ms;
        _fill_bucket(&_global_bucket, _global_amount, _global_burst);
        uint32_t _perTopic_amount = (_time_ms - _perTopic_fillTime_ms) / _perTopic_period_ms;
        _perTopic_fillTime_ms += _perTopic_amount * _perTopic_period_ms;
        for (uint8_t i = 0; i < Config::LOG_TOPIC_MAX; ++i) {
            _fill_bucket(&_perTopic_buckets[i], _perTopic_amount, _perTopic_burst);
        }
        // TODO: emit a synthetic log entry for each bucket that has suppressed > 0,
        //       then reset its suppressed counter, so callers know messages were dropped
    }

    void Logger::push_entry(const PlcIO::LogEntry_t entry) {
        PlcIO::LogTopic topic = entry.topic >= 
            Config::LOG_TOPIC_MAX ? LogTopic::NONE : entry.topic;                       // determine the topic to use for this entry
        LogBucket_t* topic_bucket = &_perTopic_buckets[topic];                          // get the bucket for this topic
        if (_try_bucket(&_global_bucket))                                               // apply the global filter
            if (topic == LogTopic::NONE || _try_bucket(topic_bucket))                   // if the entry has a topic, apply the per-topic filter
                for (uint8_t i = 0; i < _writerCount; i++)                              // then, for each registered writer
                    _writers[i]->push(&entry);                                          // push the entry to the writer's queue
    }


}}