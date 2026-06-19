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

using namespace Botanik;
using LogEntry_t = PlcIO::LogEntry_t;

namespace {

    // A writer that records what it is handed, with a settable loss counter so we
    // can drive the Logger's sink-drop reporting.
    struct RecordingWriter : Logs::LogWriter {
        int             pushes     = 0;
        PlcIO::LogLevel last_level  = PlcIO::LOG_DEBUG;
        char            last_msg[Config::LOG_PLC_MESSAGE_LEN] = {0};
        uint32_t        loss        = 0;
        void push(const PlcIO::LogEntry_t* e) override {
            pushes++; last_level = e->level;
            memcpy(last_msg, e->message, sizeof(last_msg));
        }
        void drain() override {}
        uint32_t loss_count() const override { return loss; }
        size_t format(const PlcIO::LogEntry_t*, char*, size_t) override { return 0; }
        size_t write(const char*, size_t) override { return 0; }
    };

    // A BufferedLogWriter whose sink always accepts, recording the timestamps it
    // drains so we can see which entries survived the queue.
    struct TestBuffered : Logs::BufferedLogWriter {
        std::vector<uint32_t> sent;
        size_t format(const PlcIO::LogEntry_t* e, char* buf, size_t len) override {
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
    LogEntry_t info_on(PlcIO::LogTopic topic) {
        LogEntry_t e = {};
        e.level = PlcIO::LOG_INFO;
        e.topic = topic;
        return e;
    }

}

TEST_CASE("writer: drain delivers queued entries in order") {
    TestBuffered w;
    LogEntry_t e = {}; e.level = PlcIO::LOG_INFO;
    for (uint32_t i = 1; i <= 5; ++i) { e.timestamp = i; w.push(&e); }
    w.drain();
    CHECK(w.sent.size() == 5);
    for (uint32_t i = 0; i < 5; ++i) CHECK(w.sent[i] == i + 1);
    CHECK(w.loss_count() == 0);
}

TEST_CASE("writer: a critical evicts the oldest entry on a full queue") {
    TestBuffered w;
    LogEntry_t e = {}; e.level = PlcIO::LOG_INFO;
    // Fill the ring (usable capacity is LOG_QUEUE_SIZE - 1).
    for (uint32_t i = 1; i <= Config::LOG_QUEUE_SIZE - 1; ++i) { e.timestamp = i; w.push(&e); }
    CHECK(w.loss_count() == 0);
    e.timestamp = 100; w.push(&e);              // full: a non-critical is dropped
    CHECK(w.loss_count() == 1);
    e.level = PlcIO::LOG_CRITICAL; e.timestamp = 200; w.push(&e);  // evicts oldest, seats critical
    CHECK(w.loss_count() == 2);
    w.drain();
    CHECK(w.sent.size() == Config::LOG_QUEUE_SIZE - 1);
    CHECK(w.sent.front() == 2);                 // ts=1 was evicted
    CHECK(w.sent.back() == 200);                // the critical survived
    // the dropped non-critical (ts=100) never made it into the queue
    CHECK(std::find(w.sent.begin(), w.sent.end(), 100u) == w.sent.end());
}

TEST_CASE("logger: sink-drop report is edge-triggered, critical, held off") {
    Logs::Logger logger;
    RecordingWriter w;
    logger.add_writer(&w);

    logger.step();                              // no loss yet -> nothing reported
    int base = w.pushes;

    w.loss = 7;
    logger.step();                              // first loss reports instantly
    CHECK(w.pushes == base + 1);
    CHECK(w.last_level == PlcIO::LOG_CRITICAL);
    CHECK(std::string(w.last_msg).find("7") != std::string::npos);
    CHECK(std::string(w.last_msg).find("dropped at sink") != std::string::npos);

    w.loss = 12;
    logger.step();                              // within the holdoff -> no new report
    CHECK(w.pushes == base + 1);
}

// Token-bucket mechanics (direct)
// ----------------------------------------------------------------------------

TEST_CASE("bucket: _try_bucket consumes a token and counts, then suppresses") {
    TestLogger lg;
    Logs::LogBucket_t b;
    b.state = 2;
    CHECK(lg._try_bucket(&b));                   // 2 -> 1
    CHECK(lg._try_bucket(&b));                   // 1 -> 0
    CHECK(b.state == 0);
    CHECK(b.count == 2);
    CHECK_FALSE(lg._try_bucket(&b));             // empty now
    CHECK_FALSE(lg._try_bucket(&b));
    CHECK(b.suppressed == 2);                    // drops are tallied
    CHECK(b.count == 2);                         // count untouched while suppressing
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
    lg._report_suppressed(&b, PlcIO::PLC_IO, 1234);
    CHECK(w.pushes == 1);
    CHECK(w.last_level == PlcIO::LOG_CRITICAL);   // critical so the writer can't drop it
    CHECK(std::string(w.last_msg).find("5") != std::string::npos);
    CHECK(std::string(w.last_msg).find("suppressed") != std::string::npos);
    CHECK(b.suppressed == 0);                     // drained after reporting
}

TEST_CASE("bucket: _report_suppressed with nothing suppressed emits nothing") {
    TestLogger lg;
    RecordingWriter w;
    lg.add_writer(&w);
    Logs::LogBucket_t b;                          // suppressed == 0
    lg._report_suppressed(&b, PlcIO::PLC_IO, 1);
    CHECK(w.pushes == 0);
}

// Dual-bucket filtering (through push_entry, against the constructor's full buckets)
// ----------------------------------------------------------------------------

TEST_CASE("rate limit: a flooded topic passes up to the burst, then drops") {
    Logs::Logger lg;
    RecordingWriter w;
    lg.add_writer(&w);
    // Global and per-topic bursts are equal, so a single topic deplete both in
    // lockstep: exactly `burst` entries survive.
    const int burst = Config::LOG_GLOBAL_BUCKET_BURST;
    LogEntry_t e = info_on(PlcIO::PLC_IO);
    for (int i = 0; i < burst + 10; ++i) lg.push_entry(e);
    CHECK(w.pushes == burst);
}

TEST_CASE("rate limit: the global bucket caps total throughput across topics") {
    Logs::Logger lg;
    RecordingWriter w;
    lg.add_writer(&w);
    const int burst = Config::LOG_GLOBAL_BUCKET_BURST;
    LogEntry_t a = info_on(PlcIO::PLC_IO);
    LogEntry_t b = info_on(PlcIO::BOOT);
    for (int i = 0; i < 15; ++i) lg.push_entry(a);       // global 20->5
    for (int i = 0; i < 15; ++i) lg.push_entry(b);       // topic B has tokens, but
                                                         // global (5) gates it
    CHECK(w.pushes == burst);                            // 15 + 5: global is the cap
}

TEST_CASE("rate limit: topic NONE skips the per-topic bucket but still hits global") {
    Logs::Logger lg;
    RecordingWriter w;
    lg.add_writer(&w);
    const int burst = Config::LOG_GLOBAL_BUCKET_BURST;
    LogEntry_t e = info_on(PlcIO::NONE);
    for (int i = 0; i < burst + 5; ++i) lg.push_entry(e);
    CHECK(w.pushes == burst);                            // limited by the global bucket alone
}

TEST_CASE("rate limit: an out-of-range topic is treated as NONE (no OOB bucket access)") {
    Logs::Logger lg;
    RecordingWriter w;
    lg.add_writer(&w);
    const int burst = Config::LOG_GLOBAL_BUCKET_BURST;
    LogEntry_t e = info_on((PlcIO::LogTopic)Config::LOG_TOPIC_MAX); // past every defined topic
    for (int i = 0; i < burst + 5; ++i) lg.push_entry(e);
    CHECK(w.pushes == burst);
}
