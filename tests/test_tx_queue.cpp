/**
 * @file test_tx_queue.cpp
 * @brief Unit tests for TxScheduler: priority draining, keyed coalescing, atomic
 *        (no partial) emit under budget/room limits, the oversized escape,
 *        drop-by-owner, and pool-overflow eviction. The emitter is mocked; the
 *        scheduler owns the priority + budget + atomicity logic exercised here.
 */
#include <cstdint>
#include <cstring>
#include <string>

#include "doctest.h"

#include "libbotanik_config.h"
#include "libbotanik/console/tx_queue.h"

using namespace Botanik::Terminal;

namespace {
    // Stands in for the terminal: appends content, models the ring via a settable
    // room ceiling (decremented by the content bytes), and returns 0 when the
    // reserved size (content + TX_MOVE_RESERVE) does not fit - the atomicity path.
    struct MockEmitter : public TxEmitter {
        std::string out;
        uint16_t    room;
        explicit MockEmitter(uint16_t r = 60000) : room(r) {}
        uint16_t emit(uint16_t, uint16_t, const char* data, uint16_t len,
                      uint16_t, uint16_t) override {
            uint16_t need = (uint16_t)(len + TX_MOVE_RESERVE);
            if (room < need) return 0;
            out.append(data, len);
            room = (uint16_t)(room - len);
            return len;
        }
    };

    // submit a NUL-terminated literal (base/end positions unused by the tests).
    bool put(TxScheduler& s, uint8_t lane, uint32_t key, const char* text) {
        return s.submit(lane, key, 0, 0, 0, 0, text, (uint16_t)strlen(text));
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

    MockEmitter em;
    s.drain(em, 60000, 60000);
    CHECK(em.out == "i1i2s1v1v2");        // input, then struct, then value (FIFO)
    CHECK(s.queued() == 0);
}

TEST_CASE("tx_queue: same key coalesces to the latest, in place") {
    TxScheduler s;
    put(s, TX_VALUE, 0x00010001u, "old");
    put(s, TX_VALUE, 0x00010002u, "keep");
    put(s, TX_VALUE, 0x00010001u, "new");   // replaces "old", keeps its position
    CHECK(s.queued() == 2);

    MockEmitter em;
    s.drain(em, 60000, 60000);
    CHECK(em.out == "newkeep");             // position preserved, value updated
}

TEST_CASE("tx_queue: a key on a different lane does not coalesce") {
    TxScheduler s;
    put(s, TX_VALUE,  0x0001u, "a");
    put(s, TX_STRUCT, 0x0001u, "b");        // same key, different lane -> distinct
    CHECK(s.queued() == 2);
}

TEST_CASE("tx_queue: atomic under a tight byte budget - never a partial emit") {
    TxScheduler s;
    put(s, TX_VALUE, 0, "0123456789");      // 10 bytes content (needs 10 + reserve)

    MockEmitter em;
    s.drain(em, /*budget=*/5, /*max_budget=*/100);     // < need, not oversized
    CHECK(em.out.empty());                  // left queued, nothing partial
    CHECK(s.queued() == 1);

    s.drain(em, /*budget=*/40, /*max_budget=*/100);    // now the reserve fits
    CHECK(em.out == "0123456789");
    CHECK(s.queued() == 0);
}

TEST_CASE("tx_queue: atomic when the ring is nearly full") {
    TxScheduler s;
    put(s, TX_VALUE, 0, "0123456789");

    MockEmitter em(4);                      // only 4 bytes of room
    s.drain(em, 60000, 60000);
    CHECK(em.out.empty());                  // room < content+reserve -> stays queued
    CHECK(s.queued() == 1);

    em.room = 100;
    s.drain(em, 60000, 60000);
    CHECK(em.out == "0123456789");
}

TEST_CASE("tx_queue: oversized transaction emits once there is room") {
    TxScheduler s;
    std::string big(50, 'X');
    s.submit(TX_STRUCT, 0, 0, 0, 0, 0, big.data(), (uint16_t)big.size());

    MockEmitter em(60000);
    s.drain(em, /*budget=*/10, /*max_budget=*/10);     // need (66) > max_budget
    CHECK(em.out == big);                   // escape: emitted despite the small budget
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

    MockEmitter em;
    s.drain(em, 60000, 60000);
    CHECK(em.out == "b1");
}

TEST_CASE("tx_queue: pool overflow evicts the oldest lowest-priority transaction") {
    TxScheduler s;
    const uint16_t N = Botanik::Config::TX_QUEUE_SLOTS;
    for (uint16_t i = 0; i < N; i++)
        s.submit(TX_VALUE, (uint32_t)(0x00990000u | i), 0, 0, 0, 0, "v", 1);
    CHECK(s.queued() == N);
    CHECK(s.dropped() == 0);

    s.submit(TX_VALUE, 0x00990000u | 0xFFFFu, 0, 0, 0, 0, "v", 1);
    CHECK(s.queued() == N);                 // capacity held
    CHECK(s.dropped() == 1);
}

TEST_CASE("tx_queue: overflow never displaces a higher-priority transaction") {
    TxScheduler s;
    const uint16_t N = Botanik::Config::TX_QUEUE_SLOTS;
    for (uint16_t i = 0; i < N; i++)
        s.submit(TX_VALUE, (uint32_t)(0x00990000u | i), 0, 0, 0, 0, "v", 1);

    put(s, TX_INPUT, 0, "K");               // high priority into a full pool
    CHECK(s.dropped() == 1);                // evicted a VALUE, not itself

    MockEmitter em;
    s.drain(em, 60000, 60000);
    CHECK(em.out.front() == 'K');           // input drained first and survived
}
