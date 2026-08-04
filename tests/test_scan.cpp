/**
 * @file test_scan.cpp
 * @brief The scan driver (scan/scan.h): multi-rate dispatch, the no-catch-up
 *        rule, that dt_ms reports the real elapsed time, and the two control
 *        authority edges.
 */
#include <cstddef>
#include <cstdint>
#include <vector>

#include "doctest.h"

#include "libbotanik/scan/scan.h"

using Botanik::Vars::Bank;
using Botanik::Plc::Program;
using Botanik::Plc::Context;

using namespace Botanik::Scan;

namespace {
    struct Rec { uint32_t dt; uint16_t missed; uint64_t now; };

    /** @brief A program that records every activation. Its state is its own -
     *  which is the point of the base class - so a test case owns its
     *  recording instead of sharing a file static with every other case. */
    class Recorder : public Program {
    public:
        std::vector<Rec> runs_seen;
        int              binds  = 0;
        int              resets = 0;

        Recorder(const char* name, uint32_t period_ms) : Program(name, period_ms) {}

        void reset() override { resets++; }

    protected:
        void bind(Botanik::Vars::Bank& io) override { (void)io; binds++; }
        void run(Context& c) override {
            runs_seen.push_back({ c.dt_ms, c.missed, c.now_ms });
        }
    };

    /** @brief Bank with control granted, which is what bring-up does. */
    struct Live {
        Bank io;
        Live() { io.enable_control(Bank::INHIBIT_HEALTH); }
    };
}

TEST_CASE("scan: each program runs at its own rate") {
    Recorder fast("fast", 100), slow("slow", 1000);
    Program* const progs[] = { &fast, &slow };
    Live f;
    Driver d(progs, 2, &f.io);
    for (uint64_t t = 0; t <= 2000; t += 10) d.step(t);

    // ~20 fast runs per 2 s, ~2 slow. Exact counts depend on the first-run
    // seeding, so assert the ratio holds rather than pinning both.
    CHECK(fast.runs_seen.size() >= 19);
    CHECK(fast.runs_seen.size() <= 21);
    CHECK(slow.runs_seen.size() >= 2);
    CHECK(slow.runs_seen.size() <= 3);
}

TEST_CASE("scan: dt_ms is the nominal period when on time") {
    Recorder fast("fast", 100);
    Program* const progs[] = { &fast };
    Live f;
    Driver d(progs, 1, &f.io);
    for (uint64_t t = 0; t <= 1000; t += 10) d.step(t);
    REQUIRE(fast.runs_seen.size() >= 3);
    for (size_t i = 1; i < fast.runs_seen.size(); i++)
        CHECK(fast.runs_seen[i].dt == 100);
}

TEST_CASE("scan: a late program runs once and reports the real elapsed time") {
    Recorder fast("fast", 100);
    Program* const progs[] = { &fast };
    Live f;
    Driver d(progs, 1, &f.io);
    d.step(0);        // first run seeds
    fast.runs_seen.clear();

    // A 950 ms stall: nine 100 ms activations were missed.
    d.step(1000);

    REQUIRE(fast.runs_seen.size() == 1);       // ran ONCE, no catch-up burst
    CHECK(fast.runs_seen[0].dt == 1000);       // and dt is the real gap, not 100
    CHECK(fast.runs_seen[0].missed == 9);
}

TEST_CASE("scan: period 0 runs every beat") {
    Recorder every("every", 0);
    Program* const progs[] = { &every };
    Live f;
    Driver d(progs, 1, &f.io);
    for (uint64_t t = 0; t < 50; t += 10) d.step(t);
    CHECK(every.runs_seen.size() == 5);
}

TEST_CASE("scan: bind runs once, before the first activation") {
    Recorder p("p", 0);
    Program* const progs[] = { &p };
    Live f;
    Driver d(progs, 1, &f.io);
    CHECK(p.binds == 0);
    for (uint64_t t = 0; t < 50; t += 10) d.step(t);
    CHECK(p.binds == 1);                       // once, not once per beat
    CHECK(p.runs_seen.size() == 5);
}

TEST_CASE("scan: same-period programs are spread across beats, not piled up") {
    // Two programs at one rate: without the stagger they would share every beat
    // forever, which is what puts the peak work per beat back where it started.
    Recorder a("a", 100), b("b", 100);
    Program* const progs[] = { &a, &b };
    Live f;
    Driver d(progs, 2, &f.io);
    for (uint64_t t = 0; t <= 1000; t += 10) d.step(t);

    // Both ran at their declared rate...
    CHECK(a.runs_seen.size() >= 9);
    CHECK(b.runs_seen.size() >= 9);
    // ...but only ever together on the first beat, which is unavoidable: a
    // program's first activation is what seeds its timer.
    size_t shared = 0;
    for (size_t i = 0; i < a.runs_seen.size(); i++)
        for (size_t j = 0; j < b.runs_seen.size(); j++)
            if (a.runs_seen[i].now == b.runs_seen[j].now) shared++;
    CHECK(shared == 1);
    CHECK(a.runs_seen[0].now == 0);
    CHECK(b.runs_seen[0].now == 0);
}

TEST_CASE("scan: an every-beat program is left alone by the stagger") {
    // period 0 has no interval to shift inside; it must keep running every beat.
    Recorder slow("slow", 100), every("every", 0);
    Program* const progs[] = { &slow, &every };
    Live f;
    Driver d(progs, 2, &f.io);
    for (uint64_t t = 0; t < 50; t += 10) d.step(t);

    CHECK(every.runs_seen.size() == 5);
    CHECK(slow.runs_seen.size() == 1);   // just the first-beat run
}

TEST_CASE("scan: programs are inhibited while control is withheld") {
    Recorder fast("fast", 100);
    Program* const progs[] = { &fast };
    Bank io;                                   // starts at INHIBIT_HEALTH
    Driver d(progs, 1, &io);
    for (uint64_t t = 0; t <= 500; t += 10) d.step(t);

    CHECK(fast.runs_seen.empty());             // nothing drives the field
    // Nothing was ever driving, so there is nothing stale to drop either.
    CHECK(fast.resets == 0);
}

TEST_CASE("scan: control being withdrawn resets the programs, once") {
    Recorder fast("fast", 100);
    Program* const progs[] = { &fast };
    Live f;
    Driver d(progs, 1, &f.io);
    d.step(0);
    REQUIRE(fast.resets == 0);

    f.io.disable_control(Bank::INHIBIT_OPERATOR);
    for (uint64_t t = 10; t <= 500; t += 10) d.step(t);

    // On the withdrawal edge, not on every beat it stays withdrawn: from here
    // the process moves without us, and what a program remembered about the
    // field is stale.
    CHECK(fast.resets == 1);
}

TEST_CASE("scan: control returning does not hand a program the whole outage") {
    Recorder fast("fast", 100);
    Program* const progs[] = { &fast };
    Live f;
    Driver d(progs, 1, &f.io);
    d.step(0);                                // one activation, seeds the timer
    fast.runs_seen.clear();

    f.io.disable_control(Bank::INHIBIT_OPERATOR);
    for (uint64_t t = 10; t <= 60000; t += 10) d.step(t);
    REQUIRE(fast.runs_seen.empty());          // inhibited throughout

    f.io.enable_control(Bank::INHIBIT_OPERATOR);
    d.step(60010);                            // the reseed beat: nothing runs yet
    d.step(60110);

    REQUIRE(fast.runs_seen.size() == 1);
    // The nominal period, NOT the 60 s outage. A Ton holding 5 s handed an hour
    // of dt fires the moment control returns, which is what a withdrawal exists
    // to prevent.
    CHECK(fast.runs_seen[0].dt == 100);
    CHECK(fast.runs_seen[0].missed == 0);
}

TEST_CASE("scan: a null table or a null program is inert") {
    Live f;
    Driver none(nullptr, 0, &f.io);
    none.step(0);                       // must not fault

    Recorder p("p", 0);
    Program* const holey[] = { nullptr, &p };
    Driver d(holey, 2, &f.io);
    d.step(0);
    CHECK(p.runs_seen.size() == 1);     // the live one still runs
}
