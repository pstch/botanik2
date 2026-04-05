#include <cstdint>
#include <cstring>
#include <cmath>

#include "../utils/serial.h"
#include "./logs.h"

using std::min;
using std::max;

// Abstract buffered log writer
// ---------------------------------------------------------------------------

// This intermediate class handles fixed-size log entry queues so that we can 
// send messages to outputs with small transmit buffers in a non-blocking way.
// This is often required for serial outputs, as they have a small output buffer,
// they often block when data is written to it.


// Log writer implementations
// ---------------------------------------------------------------------------

class SerialLogWriter : public BufferedLogWriter {
private:
    BufferedSerial& _serial;
protected:
    size_t format(const LogEntry_t* entry, char* buf, size_t len) override {
        const char* level = format_log_level(entry->level);
        const char* topic = format_log_topic(entry->topic);
        std::array<char, 22> time_buf; 
        TimeUtils::format_ms_timestamp(TimeUtils::absolute_ms(entry->timestamp), &time_buf);
        int written = snprintf(buf, len, "%s %s %s", time_buf.data(), level, topic, entry->message);
        return min((size_t)written, len);
    };
    size_t write(const char* buf, size_t len) override {
        int available = max((uint16_t)len, _serial.available());
        if (available == 0) return 0;
        return _serial.write(buf, available);
    }
public:
    SerialLogWriter(BufferedSerial& serial) : BufferedLogWriter(), _serial(serial) {}
};

class DebugLogWriter : public SerialLogWriter {
public:
    DebugLogWriter(BufferedSerial& serial): SerialLogWriter(serial) {}
};

class MonitorLogWriter : public SerialLogWriter {
protected:
    void push(const LogEntry_t* entry) override {
        if (entry->level != LOG_DEBUG) SerialLogWriter::push(entry);
    }
public:
    MonitorLogWriter(BufferedSerial& serial): SerialLogWriter(serial) {}
};


class Logger {
private:
    // global rate-limiter
    uint16_t        _global_burst           = LOG_GLOBAL_BUCKET_BURST;
    uint16_t        _global_period_ms       = LOG_GLOBAL_BUCKET_PERIOD_MS;
    uint32_t        _global_fillTime_ms     = 0;
    LogBucket_t     _global_bucket;
    // per-topic rate-limiter
    uint16_t        _perTopic_burst         = LOG_PERTOPIC_BUCKET_BURST;
    uint16_t        _perTopic_period_ms     = LOG_PERTOPIC_BUCKET_PERIOD_MS;
    uint32_t        _perTopic_fillTime_ms   = 0;
    LogBucket_t     _perTopic_buckets[LOG_TOPIC_MAX];
    // output writers list
    LogWriter*      _writers[LOG_WRITERS_MAX];
    uint8_t         _writerCount            = 0;
protected:
    // try to take a token from a bucket
    bool _try_bucket(LogBucket_t* bucket) {
        if (bucket->state <= 0) { bucket->suppressed++;             return false; }
        else                    { bucket->count++; bucket->state--; return true; }
    }
    // try to fill a bucket with the given amount of tokens
    void _fill_bucket(LogBucket_t* bucket, uint16_t amount, uint16_t burst) {
        if (amount > 0 && bucket-> state < burst) {
            bucket->state = min((uint16_t)(bucket->state + amount), burst);
        }
    }
public:
    // Constructor
    Logger() {
        _perTopic_fillTime_ms = millis();
        _global_fillTime_ms = millis();
        _global_bucket.state = _global_burst;
        for (uint8_t i = 0; i < LOG_TOPIC_MAX; ++i) {
            _perTopic_buckets[i].state = _perTopic_burst;
        }
    }    
    // Add a writer
    bool add_writer(LogWriter* writer) {
        if (writer == nullptr || _writerCount >= LOG_WRITERS_MAX) return false;    // bail out if given a nullpointer or if the writer list is full
        _writers[_writerCount++] = writer; return true;                            // store the pointer and increment writer count
    }
    // Remove a writer
    bool remove_writer(LogWriter* writer) {
        // do some checks and find the writer's current index
        if (writer == nullptr || _writerCount == 0) return false;                   // bail out if given a nullpointer of if we have no writers
        uint8_t i = 0; while (i < _writerCount && _writers[i] != writer) i++;       // find the index of the given writer
        if (i == _writerCount) return false;                                        // bail out if no writer was found
        for (uint8_t j = i; j < _writerCount - 1; ++j) _writers[j] = _writers[j+1]; // otherwise, shift subsequent writers
        _writers[--_writerCount] = nullptr; return true;                            // decrement the writer count, unset the last pointer
    }    
    // Fill rate-limiting buckets
    void fill_buckets() {
        // XXX: the choice of timer method here determines whether we want to buckets to fill in SLEEP/STOP or not.
        uint32_t _time_ms = millis();
        uint16_t _global_amount = (_time_ms - _global_fillTime_ms) / _global_period_ms;
        _global_fillTime_ms += _global_amount * _global_period_ms;
        _fill_bucket(&_global_bucket, _global_amount, _global_burst);
        uint16_t _perTopic_amount = (_time_ms - _perTopic_fillTime_ms) / _perTopic_period_ms;
        _perTopic_fillTime_ms += _perTopic_amount * _perTopic_period_ms;
        for (uint8_t i = 0; i < LOG_TOPIC_MAX; ++i) {
            _fill_bucket(&_perTopic_buckets[i], _perTopic_amount, _perTopic_burst);
        }
        // TODO: we should report suppressed messages when filling the buckets
    }
    // Push a new log entry to the writers, after applying rate-limiting
    void push_entry(const LogEntry_t entry) {
        LogTopic topic = entry.topic >= LOG_TOPIC_MAX ? LogTopic::NONE : entry.topic;  // determine the topic to use for this entry
        LogBucket_t* topic_bucket = &_perTopic_buckets[topic];                         // get the bucket for this topic
        if (_try_bucket(&_global_bucket))                                              // apply the global filter
            if (topic == LogTopic::NONE || _try_bucket(topic_bucket))                  // if the entry has a topic, apply the per-topic filter
                for (uint8_t i = 0; i < _writerCount; i++)                             // then, for each registered writer
                    _writers[i]->push(&entry);                                          // push the entry to the writer's queue
    }
};
