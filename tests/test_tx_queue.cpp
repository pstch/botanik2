/**
 * @file test_tx_queue.cpp
 * @brief Unit tests for TxScheduler: priority draining, keyed coalescing, atomic
 *        (no partial) emit under budget/room limits, the oversized escape,
 *        drop-by-owner, and pool-overflow eviction.
 */
#include <cstdint>
#include <cstring>
#include <string>

#include "doctest.h"

#include "libbotanik_config.h"
#include "libbotanik/console/tx_queue.h"

using namespace Botanik::Terminal;

namespace {
    // Captures drained bytes; room() is a settable ceiling decremented on write,
    // so a test can model a full/near-full ring.
    struct CaptureSink : public TxSink {
        std::string out;
        uint16_t    _room;
        explicit CaptureSink(uint16_t room = 60000) : _room(room) {}
        uint16_t room() const override { return _room; }
        void write(const char* b, uint16_t n) override {
            out.append(b, n);
            _room = (n <= _room) ? (uint16_t)(_room - n) : 0;
        }
    };

    // submit a NUL-terminated literal as a transaction body.
    bool put(TxScheduler& s, uint8_t lane, uint32_t key, const char* text) {
        return s.submit(lane, key, text, (uint16_t)strlen(text));
    }
}

TEST_CASE("tx_queue: drains highest lane first, FIFO within a lane") {
    TxScheduler s;
    put(s, TX_VALUE,  0, "v1");
    put(s, TX_STRUCT, 0, "s1");
    put(s, TX_INPUT,  0, "i1");
    put(s, TX_VALUE,  0, "v2");
    put(s, TX_INPUT,  0, "i2");
    CHECK(s.queued() == 5);

    CaptureSink sink;
    s.drain(sink, 60000, 60000);
    // input lane first (FIFO), then struct, then value (FIFO).
    CHECK(sink.out == "i1i2s1v1v2");
    CHECK(s.queued() == 0);
}

TEST_CASE("tx_queue: same key coalesces to the latest, in place") {
    TxScheduler s;
    put(s, TX_VALUE, 0x00010001u, "old");
    put(s, TX_VALUE, 0x00010002u, "keep");
    put(s, TX_VALUE, 0x00010001u, "new");   // replaces "old", keeps its position
    CHECK(s.queued() == 2);

    CaptureSink sink;
    s.drain(sink, 60000, 60000);
    CHECK(sink.out == "newkeep");           // position preserved, value updated
}

TEST_CASE("tx_queue: a key on a different lane does not coalesce") {
    TxScheduler s;
    put(s, TX_VALUE,  0x0001u, "a");
    put(s, TX_STRUCT, 0x0001u, "b");        // same key, different lane -> distinct
    CHECK(s.queued() == 2);
}

TEST_CASE("tx_queue: atomic under a tight byte budget - never a partial write") {
    TxScheduler s;
    put(s, TX_VALUE, 0, "0123456789");      // 10 bytes

    CaptureSink sink;
    s.drain(sink, /*budget=*/5, /*max_budget=*/100);   // budget < len, not oversized
    CHECK(sink.out.empty());                // left queued, nothing partial
    CHECK(s.queued() == 1);

    s.drain(sink, /*budget=*/20, /*max_budget=*/100);  // now it fits
    CHECK(sink.out == "0123456789");
    CHECK(s.queued() == 0);
}

TEST_CASE("tx_queue: atomic under a nearly-full sink") {
    TxScheduler s;
    put(s, TX_VALUE, 0, "0123456789");

    CaptureSink sink(4);                    // only 4 bytes of room
    s.drain(sink, 60000, 60000);
    CHECK(sink.out.empty());                // room < len -> stays queued
    CHECK(s.queued() == 1);

    sink._room = 100;
    s.drain(sink, 60000, 60000);
    CHECK(sink.out == "0123456789");
}

TEST_CASE("tx_queue: oversized transaction emits once there is room") {
    TxScheduler s;
    std::string big(50, 'X');
    s.submit(TX_STRUCT, 0, big.data(), (uint16_t)big.size());

    CaptureSink sink(60000);
    s.drain(sink, /*budget=*/10, /*max_budget=*/10);   // len (50) > max_budget
    CHECK(sink.out == big);                 // escape: emitted despite the small budget
    CHECK(s.queued() == 0);
}

TEST_CASE("tx_queue: drop_owner removes only the matching owner's transactions") {
    TxScheduler s;
    put(s, TX_VALUE, 0x00010001u, "a1");    // owner 1
    put(s, TX_VALUE, 0x00020001u, "b1");    // owner 2
    put(s, TX_VALUE, 0x00010002u, "a2");    // owner 1
    CHECK(s.queued() == 3);

    s.drop_owner(1);
    CHECK(s.queued() == 1);
    CHECK(s.dropped() == 0);                // an intentional drop is not an overflow

    CaptureSink sink;
    s.drain(sink, 60000, 60000);
    CHECK(sink.out == "b1");
}

TEST_CASE("tx_queue: pool overflow evicts the oldest lowest-priority transaction") {
    TxScheduler s;
    const uint16_t N = Botanik::Config::TX_QUEUE_SLOTS;
    // Fill the pool with VALUE transactions (distinct keys so none coalesce).
    for (uint16_t i = 0; i < N; i++)
        s.submit(TX_VALUE, (uint32_t)(0x00990000u | i), "v", 1);
    CHECK(s.queued() == N);
    CHECK(s.dropped() == 0);

    // One more VALUE evicts the oldest queued (still lowest priority) one.
    s.submit(TX_VALUE, 0x00990000u | 0xFFFFu, "v", 1);
    CHECK(s.queued() == N);                 // capacity held
    CHECK(s.dropped() == 1);
}

TEST_CASE("tx_queue: overflow never displaces a higher-priority transaction") {
    TxScheduler s;
    const uint16_t N = Botanik::Config::TX_QUEUE_SLOTS;
    for (uint16_t i = 0; i < N; i++)
        s.submit(TX_VALUE, (uint32_t)(0x00990000u | i), "v", 1);

    // A high-priority INPUT submit into a full pool evicts a VALUE, not itself.
    put(s, TX_INPUT, 0, "K");
    CHECK(s.dropped() == 1);

    CaptureSink sink;
    s.drain(sink, 60000, 60000);
    CHECK(sink.out.front() == 'K');         // the input drained first and survived
}
