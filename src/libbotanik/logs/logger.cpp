// REVIEW_35: TODO
/**
 * @file logger.cpp
 * @brief Logger implementation - token-bucket refill and dispatch.
 *
 * Dispatch to multiple writers, behind a token-bucket filter: one global bucket
 * plus one per topic. The per-topic filter applies BEFORE the global one, so a
 * message suppressed by its topic bucket does not drain the global one.
 */
#include "libbotanik/logs/logger.h"

                                // Logger

#include "libbotanik/tasks/tasks.h"

#include <algorithm>
#include <cstdio>

#include "libbotanik/hal/platform.h"
#include "libbotanik/utils/assert.h"

namespace Botanik { namespace Logs {

    namespace {
        using LogTopic = Botanik::Logs::LogTopic;
    }


    // Lifecycle

    Logger::Logger() {
        // one sample: all three anchors share it
        uint32_t now = HAL::Clock::current_ms();
        _perTopic_fillTime_ms   = now;
        _global_fillTime_ms     = now;
        _suppress_reportTime_ms = now;
        _global_bucket.state = Config::LOG_GLOBAL_BUCKET_BURST;
        for (uint8_t i = 0; i < Logs::LOG_TOPIC_COUNT; ++i) {
            _perTopic_buckets[i].state = Config::LOG_PERTOPIC_BUCKET_BURST;
        }
    }

    // Writer management

    bool Logger::add_writer(LogWriter* writer) {
#if !BOTANIK_FEATURE_LOGGING
        (void)writer;
        return false;               // nothing to fan out to
#else
        // A sink that never registered swallows everything it was meant to
        // carry, and the log is where a refusal would otherwise be reported.
        BOTANIK_ASSERT(writer != nullptr);
        BOTANIK_ASSERT(_writerCount < Config::LOG_WRITERS_MAX);
        if (writer == nullptr || _writerCount >= Config::LOG_WRITERS_MAX) return false;
        for (uint8_t i = 0; i < _writerCount; i++) if (_writers[i] == writer) return true;
        _writers[_writerCount++] = writer;
        return true;
#endif
    }

    // Dispatch

    void Logger::refill() {
#if !BOTANIK_FEATURE_LOGGING
        return;                     // no buckets to refill, nothing suppressed
#else
        // _report_* below log, and logging refills: without this the first
        // suppression report recurses.
        if (_refilling) return;
        _refilling = true;
        uint32_t now_ms         = HAL::Clock::current_ms();

        // refill the global bucket
        uint32_t global_amount  = (now_ms - _global_fillTime_ms) / Config::LOG_GLOBAL_BUCKET_PERIOD_MS;
        _global_fillTime_ms    += global_amount * Config::LOG_GLOBAL_BUCKET_PERIOD_MS;
        uint16_t global_fill = std::min(global_amount, (uint32_t)Config::LOG_GLOBAL_BUCKET_BURST);
        _fill_bucket(&_global_bucket, global_fill, Config::LOG_GLOBAL_BUCKET_BURST);

        // refill topic-specific buckets
        uint32_t topic_amount   = (now_ms - _perTopic_fillTime_ms) / Config::LOG_PERTOPIC_BUCKET_PERIOD_MS;
        _perTopic_fillTime_ms  += topic_amount * Config::LOG_PERTOPIC_BUCKET_PERIOD_MS;
        uint16_t topic_fill     = std::min(topic_amount, (uint32_t)Config::LOG_PERTOPIC_BUCKET_BURST);
        for (uint8_t i = 1; i < Logs::LOG_TOPIC_COUNT; ++i)
            _fill_bucket(&_perTopic_buckets[i], topic_fill, Config::LOG_PERTOPIC_BUCKET_BURST);

        // report suppressed messages
        uint32_t report_amount  = (now_ms - _suppress_reportTime_ms) / Config::LOG_SUPPRESS_REPORT_PERIOD_MS;
        if (report_amount > 0) {
            _suppress_reportTime_ms += report_amount * Config::LOG_SUPPRESS_REPORT_PERIOD_MS;
            // global
            _report_suppressed(&_global_bucket, LogTopic::NONE, now_ms, true);
            // skip NONE (slot 0)
            for (uint8_t i = 1; i < Logs::LOG_TOPIC_COUNT; ++i)
                _report_suppressed(&_perTopic_buckets[i], (LogTopic)i, now_ms, false);
        }
        // Data actually lost, not just rate-limited: sink drops summed across
        // writers, and TX-full drops across the watched serials. Edge-triggered
        // with a per-category holdoff, and emitted as criticals so the writers
        // never drop the report itself.
        for (uint8_t i = 0; i < _writerCount; i++)
            _report_loss(_sink_loss[i], _writers[i]->loss_count(),
                         Config::LOG_SINK_DROP_REPORT_PERIOD_MS, now_ms,
                         "log entries dropped at sink", _writers[i]->sink_name());
        uint32_t tx_total = 0;
        for (uint8_t i = 0; i < _watched_tx_count; i++)
            tx_total += _watched_tx[i]->tx_dropped();
        if (_watched_tx_count > 0)
            _report_loss(_tx_loss, tx_total, Config::LOG_TX_DROP_REPORT_PERIOD_MS,
                         now_ms, "serial bytes dropped (TX full)", nullptr);
        _refilling = false;
#endif
    }

    // Two-stage drop policy: the rate limiter applies to EVERY level, even a
    // critical storm. Whatever survives is undroppable at the WRITER FIFO,
    // which reserves slots for criticals. So "critical is undroppable" means at
    // the sink queue, not the limiter; a limiter-dropped critical is still
    // counted and summary-reported.
    void Logger::push_entry(const Logs::LogEntry& entry) {
        Logs::LogTopic topic = entry.topic >=
            Logs::LOG_TOPIC_COUNT ? LogTopic::NONE : entry.topic;
        LogBucket_t* topic_bucket = &_perTopic_buckets[topic];
        // Per-topic filter first so a per-topic-suppressed msg doesn't drain
        // global.
        if (topic != LogTopic::NONE && !_try_bucket(topic_bucket)) return;
        if (!_try_bucket(&_global_bucket)) return;
        for (uint8_t i = 0; i < _writerCount; i++) _writers[i]->push(&entry);
    }

    // Level helpers

    void Logger::debug(Logs::LogTopic topic, const char* fmt, ...) {
        va_list ap; va_start(ap, fmt); _emit(Logs::LOG_DEBUG, topic, fmt, ap); va_end(ap);
    }
    void Logger::info(Logs::LogTopic topic, const char* fmt, ...) {
        va_list ap; va_start(ap, fmt); _emit(Logs::LOG_INFO, topic, fmt, ap); va_end(ap);
    }
    void Logger::notice(Logs::LogTopic topic, const char* fmt, ...) {
        va_list ap; va_start(ap, fmt); _emit(Logs::LOG_NOTICE, topic, fmt, ap); va_end(ap);
    }
    void Logger::warning(Logs::LogTopic topic, const char* fmt, ...) {
        va_list ap; va_start(ap, fmt); _emit(Logs::LOG_WARNING, topic, fmt, ap); va_end(ap);
    }
    void Logger::error(Logs::LogTopic topic, const char* fmt, ...) {
        va_list ap; va_start(ap, fmt); _emit(Logs::LOG_ERROR, topic, fmt, ap); va_end(ap);
    }
    void Logger::critical(Logs::LogTopic topic, const char* fmt, ...) {
        va_list ap; va_start(ap, fmt); _emit(Logs::LOG_CRITICAL, topic, fmt, ap); va_end(ap);
    }

    // Rate-limiting internals

    bool Logger::_try_bucket(LogBucket_t* bucket) {
        if (bucket->state == 0) {
            // Saturate rather than wrap: the slow drain cadence lets a long
            // storm otherwise overflow the uint16_t suppressed count.
            if (bucket->suppressed != UINT16_MAX) bucket->suppressed++;
            // Monotonic and never cleared, unlike the bucket's own count, which
            // the suppression report zeroes: a supervisor watching for a flood
            // needs a number that only grows.
            _dropped_total++;
            return false;
        }
        else { bucket->state--; return true; }
    }

    void Logger::_fill_bucket(LogBucket_t* bucket, uint16_t amount, uint16_t burst) {
        if (amount > 0 && bucket->state < burst) {
            bucket->state = std::min((uint16_t)(bucket->state + amount), burst);
        }
    }

    void Logger::_emit_critical(Logs::LogTopic topic, uint32_t timestamp_ms,
                                const char* fmt, ...) {
        // Straight to every writer, bypassing the rate limiter: a loss report
        // must survive the storm it summarizes.
        Logs::LogEntry entry = {};
        va_list ap; va_start(ap, fmt);
        _build_entry(entry, Logs::LOG_CRITICAL, topic, timestamp_ms, fmt, ap);
        va_end(ap);
        for (uint8_t i = 0; i < _writerCount; i++) _writers[i]->push(&entry);
    }

    void Logger::_report_suppressed(LogBucket_t* bucket, Logs::LogTopic topic,
                                    uint32_t timestamp_ms, bool global) {
        if (bucket->suppressed == 0) return;
        _emit_critical(topic, timestamp_ms, "%u msgs suppressed (%s limit)",
                       (unsigned)bucket->suppressed,
                       global ? "global" : "topic");
        bucket->suppressed = 0;
    }

    void Logger::_report_loss(LossReporter_t& rep, uint32_t total, uint32_t interval_ms,
                              uint32_t now_ms, const char* what, const char* who) {
        if (total <= rep.reported_count) return;   // nothing new
        if (rep.reported && (now_ms - rep.last_report_ms) < interval_ms)
            return;   // holdoff
        unsigned delta = (unsigned)(total - rep.reported_count);
        if (who != nullptr) _emit_critical(LogTopic::NONE, now_ms, "%s: %u %s", who, delta, what);
        else                _emit_critical(LogTopic::NONE, now_ms, "%u %s", delta, what);
        rep.reported_count = total;
        rep.last_report_ms = now_ms;
        rep.reported       = true;
    }

    void Logger::_build_entry(Logs::LogEntry& e, Logs::LogLevel level, Logs::LogTopic topic,
                              uint32_t timestamp_ms, const char* fmt, va_list ap) {
        e.timestamp = timestamp_ms;
        e.level     = level;
        e.topic     = topic;
        int w = vsnprintf(e.message, sizeof(e.message), fmt, ap);
        if (w < 0) w = 0;
        e.length = (uint16_t)std::min((size_t)w, sizeof(e.message) - 1);
    }

    void Logger::_emit(Logs::LogLevel level, Logs::LogTopic topic,
                       const char* fmt, va_list ap) {
#if !BOTANIK_FEATURE_LOGGING
        // The one funnel every level helper reaches, and where the vsnprintf
        // happens: returning here is what makes a logging-off build cheap
        // without changing a call site or a header holding a Logger*.
        (void)level; (void)topic; (void)fmt; (void)ap;
        return;
#else
        // Before the buckets are read, not on a beat: a token bucket adds for
        // the time ELAPSED, so this is where it is exact. Re-entrant-safe (the
        // loss reports it emits come back through here).
        refill();
        Logs::LogEntry entry = {};
        _build_entry(entry, level, topic, HAL::Clock::current_ms(), fmt, ap);
        push_entry(entry);
#endif
    }

}}

namespace Botanik { namespace Logs {

}}  // namespace Botanik::Logs
