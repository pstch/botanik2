/**
 * @file test_functions.cpp
 * @brief The PLC functions (plc/functions/). Stateless, so every case is one
 *        line - which is the point of the split from the blocks.
 */
#include "doctest.h"

#include "libbotanik/plc/functions/functions.h"

using namespace Botanik::Plc;

TEST_CASE("limit holds a value inside its bounds") {
    CHECK(limit(5.0f, 0.0f, 10.0f) == doctest::Approx(5.0f));
    CHECK(limit(-1.0f, 0.0f, 10.0f) == doctest::Approx(0.0f));
    CHECK(limit(99.0f, 0.0f, 10.0f) == doctest::Approx(10.0f));
    // Constant-folded: the whole reason these are constexpr rather than a .cpp.
    static_assert(limit(99.0f, 0.0f, 10.0f) == 10.0f, "limit is constexpr");
}

TEST_CASE("in_range is inclusive at both ends") {
    CHECK(in_range(230.0f, 200.0f, 260.0f));
    CHECK(in_range(200.0f, 200.0f, 260.0f));
    CHECK(in_range(260.0f, 200.0f, 260.0f));
    CHECK_FALSE(in_range(199.9f, 200.0f, 260.0f));
}

TEST_CASE("deadzone reports the excursion, and is continuous at the edge") {
    CHECK(deadzone(0.3f, 0.5f) == doctest::Approx(0.0f));
    CHECK(deadzone(-0.3f, 0.5f) == doctest::Approx(0.0f));
    // No step out of the band: leaving it by a hair gives a hair, not `band`.
    CHECK(deadzone(0.5001f, 0.5f) == doctest::Approx(0.0001f));
    CHECK(deadzone(2.0f, 0.5f) == doctest::Approx(1.5f));
    CHECK(deadzone(-2.0f, 0.5f) == doctest::Approx(-1.5f));
}

TEST_CASE("scale rescales linearly, and a dead span reads as the low end") {
    CHECK(scale(0.5f, 0.0f, 1.0f, 20.0f, 80.0f) == doctest::Approx(50.0f));
    CHECK(scale(0.0f, 0.0f, 1.0f, 20.0f, 80.0f) == doctest::Approx(20.0f));
    CHECK(scale(2.0f, 0.0f, 1.0f, 20.0f, 80.0f) == doctest::Approx(140.0f));  // no clamp
    CHECK(scale(1.0f, 0.0f, 1.0f, 80.0f, 20.0f) == doctest::Approx(20.0f));   // inverted
    // A misconfigured span must give a dull answer, not an infinity that then
    // propagates into an output.
    CHECK(scale(7.0f, 3.0f, 3.0f, 20.0f, 80.0f) == doctest::Approx(20.0f));
}

TEST_CASE("lerp and sel") {
    CHECK(lerp(10.0f, 20.0f, 0.25f) == doctest::Approx(12.5f));
    CHECK(sel(false, 18.0f, 21.0f) == doctest::Approx(18.0f));
    CHECK(sel(true, 18.0f, 21.0f) == doctest::Approx(21.0f));
}

TEST_CASE("seconds converts the scan's dt") {
    CHECK(seconds(1000) == doctest::Approx(1.0f));
    CHECK(seconds(250) == doctest::Approx(0.25f));
    CHECK(seconds(0) == doctest::Approx(0.0f));
}

TEST_CASE("accumulate saturates at the limit rather than wrapping") {
    uint32_t e = 0;
    accumulate(e, 100, 40);
    CHECK(e == 40);
    accumulate(e, 100, 40);
    CHECK(e == 80);
    // A hold far longer than the limit stays elapsed.
    accumulate(e, 100, 0xFFFFFFFFu);
    CHECK(e == 100);
    accumulate(e, 100, 50);
    CHECK(e == 100);
}

TEST_CASE("countdown stops at zero rather than wrapping") {
    uint32_t r = 100;
    countdown(r, 40);
    CHECK(r == 60);
    countdown(r, 1000);
    CHECK(r == 0);
    countdown(r, 10);
    CHECK(r == 0);
}
