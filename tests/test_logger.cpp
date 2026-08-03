/**
 * @file test_logger.cpp
 * @brief Tests for the logging loss-protection paths added in the review:
 *        the writer's critical-eviction (REPORT-SLOT) and the Logger's
 *        edge-triggered sink-drop reporting (REPORT-EDGE).
 */
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "doctest.h"

#include "libbotanik/logs/logger.h"
#include "recording_writer.h"

using namespace Botanik;

namespace {

    // A BufferedLogWriter whose sink always accepts, recording the timestamps it
    // drains so we can see which entries survived the queue.
    struct TestBuffered : Logs::BufferedLogWriter {
        std::vector<uint32_t> sent;
        size_t format(const Logs::LogEntry* e, char* buf, size_t len) override {
            (void)len; sent.push_back(e->timestamp); buf[0] = 'x'; return 1;
        }
        size_t write(const char* buf, size_t len) override { (void)buf; return len; }
    };

    // Exposes the protected token-bucket primitives so the rate-limiter mechanics
    // can be driven directly, independent of the (real, un-mockable) clock.
    struct TestLogger : Logs::Logger {
        using Logs::Logger::_try_bucket;
        using Logs::Logger::_fill_bucket;
        using Logs::Logger::_report_suppressed;
    };

    // A LOG_INFO entry on a given topic (the level the limiter actually gates;
    // criticals bypass the writer queue, not the buckets).
    Logs::LogEntry info_on(Logs::LogTopic topic) {
        Logs::LogEntry e = {};
        e.level = Logs::LOG_INFO;
        e.topic = topic;
        return e;
    }

}

TEST_CASE("writer: drain delivers queued entries in order") {
    TestBuffered w;
    Logs::LogEntry e = {}; e.level = Logs::LOG_INFO;
    for (uint32_t i = 1; i <= 5; ++i) { e.timestamp = i; w.push(&e); }
    w.drain();
    CHECK(w.sent.size() == 5);
    for (uint32_t i = 0; i < 5; ++i) CHECK(w.sent[i] == i + 1);
    CHECK(w.loss_count() == 0);
}

TEST_CASE("writer: criticals seat into the reserve without evicting normals") {
    TestBuffered w;
    const uint32_t NORMAL_CAP = Config::LOG_QUEUE_SIZE - 1 - Config::LOG_QUEUE_CRITICAL_RESERVE;
    Logs::LogEntry e = {}; e.level = Logs::LOG_INFO;
    // Normals fill only up to NORMAL_CAP; the last K slots are reserved for criticals.
    for (uint32_t i = 1; i <= NORMAL_CAP; ++i) { e.timestamp = i; w.push(&e); }
    CHECK(w.loss_count() == 0);
    e.timestamp = 1000; w.push(&e);                 // would intrude on the reserve: dropped
    CHECK(w.loss_count() == 1);
    // Criticals fill the reserve WITHOUT evicting any normal - the key fix that stops the
    // sink-loss report from manufacturing the drop it reports.
    e.level = Logs::LOG_CRITICAL;
    for (uint32_t i = 0; i < Config::LOG_QUEUE_CRITICAL_RESERVE; ++i) { e.timestamp = 200 + i; w.push(&e); }
    CHECK(w.loss_count() == 1);                     // no eviction occurred
    // Only now (completely full) does a further critical evict the oldest as a last resort.
    e.timestamp = 999; w.push(&e);
    CHECK(w.loss_count() == 2);
    w.drain();
    CHECK(w.sent.size() == Config::LOG_QUEUE_SIZE - 1);
    CHECK(w.sent.front() == 2);                     // ts=1 evicted by the last-resort critical
    CHECK(w.sent.back() == 999);                    // that critical seated last
    CHECK(std::find(w.sent.begin(), w.sent.end(), 1000u) == w.sent.end());  // the dropped normal
}

TEST_CASE("logger: min_level drops sub-threshold entries, criticals always pass") {
    Logs::Logger lg;
    RecordingWriter w;
    lg.add_writer(&w);
    lg.set_min_level(Logs::LOG_WARNING);
    CHECK(lg.min_level() == Logs::LOG_WARNING);

    lg.push_entry(info_on(Logs::SHARED_IO));          // LOG_INFO < WARNING -> dropped
    CHECK(w.pushes == 0);

    Logs::LogEntry warn = info_on(Logs::SHARED_IO); warn.level = Logs::LOG_WARNING;
    lg.push_entry(warn);                            // == threshold -> passes
    CHECK(w.pushes == 1);

    Logs::LogEntry crit = info_on(Logs::SHARED_IO); crit.level = Logs::LOG_CRITICAL;
    lg.push_entry(crit);                            // critical (max level) always passes
    CHECK(w.pushes == 2);
}

TEST_CASE("logger: sink-drop report is edge-triggered, critical, held off") {
    Logs::Logger logger;
    RecordingWriter w;
    logger.add_writer(&w);

    logger.refill();                              // no loss yet -> nothing reported
    int base = w.pushes;

    w.loss = 7;
    logger.refill();                              // first loss reports instantly
    CHECK(w.pushes == base + 1);
    CHECK(w.last_level == Logs::LOG_CRITICAL);
    CHECK(std::string(w.last_msg).find("7") != std::string::npos);
    CHECK(std::string(w.last_msg).find("dropped at sink") != std::string::npos);

    w.loss = 12;
    logger.refill();                              // within the holdoff -> no new report
    CHECK(w.pushes == base + 1);
}

// Token-bucket mechanics (direct)
// ----------------------------------------------------------------------------

TEST_CASE("bucket: _try_bucket consumes a token, then suppresses") {
    TestLogger lg;
    Logs::LogBucket_t b;
    b.state = 2;
    CHECK(lg._try_bucket(&b));                   // 2 -> 1
    CHECK(lg._try_bucket(&b));                   // 1 -> 0
    CHECK(b.state == 0);
    CHECK_FALSE(lg._try_bucket(&b));             // empty now
    CHECK_FALSE(lg._try_bucket(&b));
    CHECK(b.suppressed == 2);                    // drops are tallied
}

TEST_CASE("bucket: the suppressed counter saturates instead of wrapping") {
    TestLogger lg;
    Logs::LogBucket_t b;
    b.state = 0;
    b.suppressed = UINT16_MAX;
    CHECK_FALSE(lg._try_bucket(&b));
    CHECK(b.suppressed == UINT16_MAX);           // clamped, no wrap to 0
}

TEST_CASE("bucket: _fill_bucket tops up to the burst ceiling only") {
    TestLogger lg;
    Logs::LogBucket_t b;
    b.state = 5;
    lg._fill_bucket(&b, 10, 20);
    CHECK(b.state == 15);
    lg._fill_bucket(&b, 100, 20);                // overshoot clamps to burst
    CHECK(b.state == 20);
    lg._fill_bucket(&b, 5, 20);                  // already full: no-op
    CHECK(b.state == 20);
    b.state = 7;
    lg._fill_bucket(&b, 0, 20);                  // zero refill: no-op
    CHECK(b.state == 7);
}

TEST_CASE("bucket: _report_suppressed emits a critical summary and clears the tally") {
    TestLogger lg;
    RecordingWriter w;
    lg.add_writer(&w);
    Logs::LogBucket_t b;
    b.suppressed = 5;
    lg._report_suppressed(&b, Logs::SHARED_IO, 1234);
    CHECK(w.pushes == 1);
    CHECK(w.last_level == Logs::LOG_CRITICAL);   // critical so the writer can't drop it
    CHECK(std::string(w.last_msg).find("5") != std::string::npos);
    CHECK(std::string(w.last_msg).find("suppressed") != std::string::npos);
    CHECK(b.suppressed == 0);                     // drained after reporting
}

TEST_CASE("bucket: _report_suppressed with nothing suppressed emits nothing") {
    TestLogger lg;
    RecordingWriter w;
    lg.add_writer(&w);
    Logs::LogBucket_t b;                          // suppressed == 0
    lg._report_suppressed(&b, Logs::SHARED_IO, 1);
    CHECK(w.pushes == 0);
}

// Dual-bucket filtering (through push_entry, against the constructor's full buckets)
// ----------------------------------------------------------------------------

TEST_CASE("rate limit: a flooded topic passes up to the burst, then drops") {
    Logs::Logger lg;
    RecordingWriter w;
    lg.add_writer(&w);
    // A single topic is gated by the smaller of its per-topic bucket and the
    // shared global bucket.
    const int gburst = Config::LOG_GLOBAL_BUCKET_BURST;
    const int tburst = Config::LOG_PERTOPIC_BUCKET_BURST;
    const int cap = gburst < tburst ? gburst : tburst;
    Logs::LogEntry e = info_on(Logs::SHARED_IO);
    for (int i = 0; i < cap + 10; ++i) lg.push_entry(e);
    CHECK(w.pushes == cap);
}

TEST_CASE("rate limit: the global bucket caps total throughput across topics") {
    Logs::Logger lg;
    RecordingWriter w;
    lg.add_writer(&w);
    // Exhaust topic A's per-topic bucket (global drops by tburst), then flood a
    // second topic: B still has per-topic tokens, but the global remainder gates
    // it, so the total is the global burst. (Assumes tburst <= gburst <= 2*tburst.)
    const int gburst = Config::LOG_GLOBAL_BUCKET_BURST;
    const int tburst = Config::LOG_PERTOPIC_BUCKET_BURST;
    Logs::LogEntry a = info_on(Logs::SHARED_IO);
    Logs::LogEntry b = info_on(Logs::BOOT);
    for (int i = 0; i < tburst; ++i) lg.push_entry(a);   // A's topic bucket -> 0, global -= tburst
    for (int i = 0; i < tburst; ++i) lg.push_entry(b);   // B has topic tokens, global (gburst-tburst) gates it
    CHECK(w.pushes == gburst);                           // tburst + (gburst - tburst): global is the cap
}

TEST_CASE("rate limit: topics with no per-topic bucket are gated by the global bucket alone") {
    Logs::Logger lg;
    RecordingWriter w;
    lg.add_writer(&w);
    const int burst = Config::LOG_GLOBAL_BUCKET_BURST;
    Logs::LogTopic topic = Logs::NONE;
    SUBCASE("NONE skips the per-topic bucket")           { topic = Logs::NONE; }
    // Out of range is the no-OOB-bucket-access regression: it must be clamped to NONE.
    SUBCASE("an out-of-range topic is treated as NONE")  { topic = (Logs::LogTopic)Config::LOG_TOPIC_MAX; }
    Logs::LogEntry e = info_on(topic);
    for (int i = 0; i < burst + 5; ++i) lg.push_entry(e);
    CHECK(w.pushes == burst);                            // limited by the global bucket alone
}
