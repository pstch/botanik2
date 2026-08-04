/**
 * @file test_component.cpp
 * @brief Component (core/component.h): the step() contract, and the per-component
 *        period that lets one sit out beats without the loop knowing.
 */
#include <cstdint>
#include <vector>

#include "doctest.h"

#include "libbotanik/core/component.h"

using Botanik::Component;

namespace {
    /** A component that records every beat it actually ran on. */
    class Spy : public Component {
    public:
        std::vector<uint64_t> ran;
        std::vector<uint32_t> dts;
        std::vector<uint16_t> misses;
        uint32_t setups = 0;
        uint32_t serial = 0;
        uint32_t reconfigs = 0;

        Spy() : Component(nullptr, "spy") {}

    protected:
        void on_setup(uint64_t) override { setups++; }
        void on_step(uint64_t now_ms) override {
            ran.push_back(now_ms);
            dts.push_back(dt_ms());
            misses.push_back(missed());
        }
        uint32_t param_serial() const override { return serial; }
        void on_param_changed(uint64_t) override { reconfigs++; }
    };
}

TEST_CASE("component: the default period is every beat") {
    Spy s;
    CHECK(s.period_ms() == 0);
    for (uint64_t t = 0; t < 50; t += 10) s.step(t);
    CHECK(s.ran.size() == 5);
}

TEST_CASE("component: a period makes it sit out the beats in between") {
    Spy s;
    s.set_period(50);
    for (uint64_t t = 0; t <= 200; t += 10) s.step(t);

    // t=0 (first step is always due), then 50/100/150/200.
    REQUIRE(s.ran.size() == 5);
    CHECK(s.ran[0] == 0);
    CHECK(s.ran[1] == 50);
    CHECK(s.ran[4] == 200);
}

TEST_CASE("component: on_setup() still runs on the first beat, whatever the period") {
    Spy s;
    s.set_period(1000);          // far longer than the run below
    for (uint64_t t = 0; t < 100; t += 10) s.step(t);

    CHECK(s.setups == 1);        // ran, despite never coming due again
    CHECK(s.ran.size() == 1);
}

TEST_CASE("component: dt_ms is the real gap, not the beat") {
    Spy s;
    s.set_period(50);
    for (uint64_t t = 0; t <= 150; t += 10) s.step(t);

    REQUIRE(s.dts.size() >= 3);
    CHECK(s.dts[0] == 50);       // first run reports the nominal period
    CHECK(s.dts[1] == 50);       // then the real gap, which here matches
    CHECK(s.dts[2] == 50);
}

TEST_CASE("component: a stalled loop reports the real gap and the dropped beats") {
    Spy s;
    s.set_period(50);
    s.step(0);
    s.ran.clear(); s.dts.clear(); s.misses.clear();

    s.step(500);                 // a 500 ms stall on a 50 ms period

    REQUIRE(s.ran.size() == 1);  // ran ONCE, no catch-up burst
    CHECK(s.dts[0] == 500);      // the real elapsed time
    CHECK(s.misses[0] == 9);
}

TEST_CASE("component: a sat-out beat does not poll the config serial either") {
    Spy s;
    s.set_period(100);
    s.step(0);
    CHECK(s.reconfigs == 0);     // serial 0 at boot is not a change

    s.serial = 7;                // config commits between activations
    s.step(10);
    s.step(20);
    CHECK(s.reconfigs == 0);     // not due, so not noticed yet

    s.step(100);
    CHECK(s.reconfigs == 1);     // picked up on the next activation
}

TEST_CASE("component: period 0 and an unset period behave identically") {
    Spy a, b;
    b.set_period(0);
    for (uint64_t t = 0; t < 100; t += 10) { a.step(t); b.step(t); }
    CHECK(a.ran == b.ran);
    CHECK(a.dts == b.dts);
}
