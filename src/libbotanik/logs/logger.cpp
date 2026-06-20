/**
 * @file logger.cpp
 * @brief Logger implementation - token-bucket refill and dispatch.
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

    // Lifecycle
    // ------------------------------------------------------------------------

    Logger::Logger() {
        uint32_t now = HAL::current_ms();   // one sample: all three anchors share it
        _perTopic_fillTime_ms   = now;
        _global_fillTime_ms     = now;
        _suppress_reportTime_ms = now;
        _global_bucket.state = _global_burst;
        for (uint8_t i = 0; i < PlcIO::LOG_TOPIC_COUNT; ++i) {
            _perTopic_buckets[i].state = _perTopic_burst;
        }
    }

    // Writer management
    // ------------------------------------------------------------------------

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

    // Dispatch
    // ------------------------------------------------------------------------

    void Logger::step() {
        uint32_t now_ms         = HAL::current_ms();
        // Refill the token buckets on their own (fast) cadences. The anchor advances by
        // the FULL elapsed periods (bounded by the wrap-safe gap, so no overflow), but
        // the bucket is refilled by min(amount, burst): the bucket saturates at burst
        // anyway, and this keeps the uint16 _fill_bucket arg from truncating a long gap.
        uint32_t global_amount  = (now_ms - _global_fillTime_ms) / _global_period_ms;
        _global_fillTime_ms    += global_amount * _global_period_ms;
        _fill_bucket(&_global_bucket,
                     (uint16_t)std::min(global_amount, (uint32_t)_global_burst), _global_burst);
        uint32_t topic_amount   = (now_ms - _perTopic_fillTime_ms) / _perTopic_period_ms;
        _perTopic_fillTime_ms  += topic_amount * _perTopic_period_ms;
        uint16_t topic_fill     = (uint16_t)std::min(topic_amount, (uint32_t)_perTopic_burst);
        // Start at 1: slot 0 (NONE) is never consumed (push_entry short-circuits NONE).
        for (uint8_t i = 1; i < PlcIO::LOG_TOPIC_COUNT; ++i)
            _fill_bucket(&_perTopic_buckets[i], topic_fill, _perTopic_burst);
        // Drain suppressed-message counters on a separate, slower cadence so a
        // sustained storm yields an occasional summary rather than one per refill.
        uint32_t report_amount  = (now_ms - _suppress_reportTime_ms) / _suppress_period_ms;
        if (report_amount > 0) {
            _suppress_reportTime_ms += report_amount * _suppress_period_ms;
            _report_suppressed(&_global_bucket, LogTopic::NONE, now_ms);   // global bucket
            for (uint8_t i = 1; i < PlcIO::LOG_TOPIC_COUNT; ++i)            // per-topic, skip NONE
                _report_suppressed(&_perTopic_buckets[i], (LogTopic)i, now_ms);
        }
        // Report data actually lost (not just rate-limited): entries dropped at the
        // sinks (summed across writers) and, across the watched serials, their TX-full
        // drops. Edge-triggered with a per-category holdoff, emitted as criticals so
        // the writers never drop the report itself.
        uint32_t sink_total = 0;
        for (uint8_t i = 0; i < _writerCount; i++) sink_total += _writers[i]->loss_count();
        _report_loss(_sink_loss, sink_total, Config::LOG_SINK_DROP_REPORT_PERIOD_MS,
                     now_ms, "log entries dropped at sink");
        uint32_t tx_total = 0;
        for (uint8_t i = 0; i < _watched_tx_count; i++) tx_total += _watched_tx[i]->tx_dropped();
        if (_watched_tx_count > 0)
            _report_loss(_tx_loss, tx_total, Config::LOG_TX_DROP_REPORT_PERIOD_MS,
                         now_ms, "serial bytes dropped (TX full)");
    }

    // Two-stage drop policy: the token-bucket rate limiter below applies to EVERY level,
    // criticals included - it is a flood backstop (a critical should be rare and never hit
    // the limiter, but if a storm of them does, we must not flood the TX chain). Whatever
    // survives is then undroppable at the WRITER FIFO (it reserves slots for criticals, so
    // they're never dropped there). So "critical is undroppable" means at the sink queue,
    // not the rate limiter; a limiter-dropped critical is still counted + summary-reported.
    void Logger::push_entry(const PlcIO::LogEntry_t& entry) {
        // Level gate (before the rate limiter so a filtered entry costs no token): drop a
        // sub-threshold entry. The LogLevel values are monotonic and LOG_CRITICAL is the
        // max, so a critical never falls below _min_level - criticals always pass.
        if (entry.level < _min_level) return;
        PlcIO::LogTopic topic = entry.topic >=
            PlcIO::LOG_TOPIC_COUNT ? LogTopic::NONE : entry.topic;                       // determine the topic to use for this entry
        LogBucket_t* topic_bucket = &_perTopic_buckets[topic];                          // get the bucket for this topic
        if (topic == LogTopic::NONE || _try_bucket(topic_bucket))                         // apply the per-topic filter first so global isn't drained by per-topic-suppressed msgs
            if (_try_bucket(&_global_bucket))                                           // then apply the global filter
                for (uint8_t i = 0; i < _writerCount; i++)                              // then, for each registered writer
                    _writers[i]->push(&entry);                                          // push the entry to the writer's queue
    }

    // Level helpers
    // ------------------------------------------------------------------------

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

    // Rate-limiting internals
    // ------------------------------------------------------------------------

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

    void Logger::_report_suppressed(LogBucket_t* bucket, PlcIO::LogTopic topic, uint32_t timestamp_ms) {
        if (bucket->suppressed == 0) return;
        LogEntry_t entry = {};
        entry.timestamp = timestamp_ms;
        // Critical so REPORT-SLOT keeps it: during the very storm it summarizes the
        // writer FIFO is full, and a lower level would be the first thing dropped.
        entry.level     = PlcIO::LOG_CRITICAL;
        entry.topic     = topic;
        int w = snprintf(
            entry.message, sizeof(entry.message),
            "%u msgs suppressed", (unsigned)bucket->suppressed);
        if (w < 0) w = 0;
        entry.length = (uint16_t)std::min((size_t)w, sizeof(entry.message) - 1);
        for (uint8_t i = 0; i < _writerCount; i++) _writers[i]->push(&entry);
        bucket->suppressed = 0;
    }

    void Logger::_report_loss(LossReporter_t& rep, uint32_t total, uint32_t interval_ms,
                              uint32_t now_ms, const char* what) {
        if (total <= rep.reported_count) return;                                  // nothing new
        if (rep.reported && (now_ms - rep.last_report_ms) < interval_ms) return;  // holdoff
        uint32_t delta = total - rep.reported_count;
        LogEntry_t entry = {};
        entry.timestamp = now_ms;
        entry.level     = PlcIO::LOG_CRITICAL;
        entry.topic     = LogTopic::NONE;
        int w = snprintf(entry.message, sizeof(entry.message), "%u %s", (unsigned)delta, what);
        if (w < 0) w = 0;
        entry.length = (uint16_t)std::min((size_t)w, sizeof(entry.message) - 1);
        for (uint8_t i = 0; i < _writerCount; i++) _writers[i]->push(&entry);
        rep.reported_count = total;
        rep.last_report_ms = now_ms;
        rep.reported       = true;
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

}}
