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
