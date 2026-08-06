/**
 * @file test_writer.cpp
 * @brief Tests for the entry ring buffer in BufferedLogWriter (logs/writer.h).
 *        Complements test_logger.cpp (which covers drain order + critical
 *        eviction) with the ring's wrap-around integrity, the partial-send
 *        resumption cursor (no re-format across drains), and format-failure
 *        accounting.
 */
#include <cstddef>                    // size_t - the queue and message lengths
#include <cstdint>                    // uint8_t/uint32_t/uint64_t - queue indices and stamps
#include <cstring>                    // memcpy / strlen - building and checking the entries
#include <vector>                     // std::vector - the drained output collected per beat

#include "libbotanik/logs/writer.h"   // Logs::BufferedLogWriter - under test (incremental drain)

#include "doctest.h"                  // the test framework (vendored single header); last so it sees our types

using namespace Botanik;

namespace {

    // A BufferedLogWriter whose sink accepts at most `cap` bytes per write() call
    // and whose format() emits a fixed-length blob. format_calls / written let a
    // test see how often an entry was formatted vs. how much actually drained.
    struct CapWriter : Logs::BufferedLogWriter {
        std::vector<uint32_t> order;     // timestamps in the order format() saw them
        int    format_calls = 0;
        size_t written      = 0;
        size_t msg_len      = 8;         // bytes each entry formats to
        size_t cap          = 4096;      // bytes write() accepts per call
        bool   fail_format  = false;

        size_t format(const Logs::LogEntry* e, char* buf, size_t len) override {
            format_calls++;
            if (fail_format) return 0;
            order.push_back(e->timestamp);
            size_t n = msg_len < len ? msg_len : len;
            std::memset(buf, 'x', n);
            return n;
        }
        size_t write(const char* buf, size_t len) override {
            (void)buf;
            size_t n = len < cap ? len : cap;
            written += n;
            return n;
        }
    };

    Logs::LogEntry make_entry(uint32_t ts) {
        Logs::LogEntry e = {};
        e.level = Logs::LOG_INFO;
        e.timestamp = ts;
        return e;
    }

}

// Ring wrap-around
// ----------------------------------------------------------------------------

TEST_CASE("writer ring: FIFO order is preserved as the cursors wrap repeatedly") {
    CapWriter w;
    // Push-then-drain many more times than the ring is deep, so read/write
    // indices wrap around LOG_QUEUE_SIZE several times. Order must stay intact.
    const uint32_t total = (uint32_t)Config::LOG_QUEUE_SIZE * 3 + 5;
    for (uint32_t i = 1; i <= total; ++i) {
        Logs::LogEntry e = make_entry(i);
        w.push(&e);
        w.drain();
    }
    REQUIRE(w.order.size() == total);
    for (uint32_t i = 0; i < total; ++i) CHECK(w.order[i] == i + 1);
    CHECK(w.loss_count() == 0);
}

TEST_CASE("writer ring: a batch that straddles the wrap boundary drains in order") {
    CapWriter w;
    // Advance the cursors close to the end of the ring one entry at a time...
    uint64_t ts = 0;
    for (int i = 0; i < (int)Config::LOG_QUEUE_SIZE - 3; ++i) {
        Logs::LogEntry e = make_entry(++ts);
        w.push(&e);
        w.drain();
    }
    // ...then queue a burst that wraps past the end before a single drain.
    w.order.clear();
    const int burst = 8;
    for (int i = 0; i < burst; ++i) { Logs::LogEntry e = make_entry(++ts); w.push(&e); }
    w.drain();
    REQUIRE(w.order.size() == (size_t)burst);
    for (int i = 0; i < burst; ++i)
        CHECK(w.order[i] == (uint32_t)(Config::LOG_QUEUE_SIZE - 3 + 1 + i));
}

// Partial-send resumption
// ----------------------------------------------------------------------------

TEST_CASE("writer: a partial write resumes without re-formatting the entry") {
    CapWriter w;
    w.msg_len = 10;
    w.cap     = 3;                       // sink takes only 3 bytes per drain()
    Logs::LogEntry e = make_entry(42);
    w.push(&e);

    w.drain();                           // formats once, sends 3
    CHECK(w.format_calls == 1);
    CHECK(w.written == 3);
    w.drain();                           // resumes: sends 3 more, no re-format
    CHECK(w.format_calls == 1);
    CHECK(w.written == 6);
    w.drain();                           // 9
    w.drain();                           // 10 (message complete)
    CHECK(w.written == 10);
    CHECK(w.format_calls == 1);

    w.drain();                           // queue empty: nothing further happens
    CHECK(w.written == 10);
    CHECK(w.format_calls == 1);
}

TEST_CASE("writer: a stalled sink (cap 0) makes no progress but never re-formats") {
    CapWriter w;
    w.msg_len = 5;
    w.cap     = 0;                       // sink refuses every byte
    Logs::LogEntry e = make_entry(1);
    w.push(&e);
    for (int i = 0; i < 4; ++i) w.drain();
    CHECK(w.written == 0);
    CHECK(w.format_calls == 1);          // formatted once, then just retried the send
}

// Format-failure accounting
// ----------------------------------------------------------------------------

TEST_CASE("writer: entries that fail to format are skipped and counted as loss") {
    CapWriter w;
    w.fail_format = true;
    Logs::LogEntry a = make_entry(1), b = make_entry(2);
    w.push(&a);
    w.push(&b);
    w.drain();
    CHECK(w.written == 0);
    CHECK(w.loss_count() == 2);          // both skipped, both counted
}
