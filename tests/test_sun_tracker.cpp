/**
 * @file test_sun_tracker.cpp
 * @brief Tests for SunPositionTracker - the geo-config-driven wrapper over the
 *        sun_tracker.h solar-elevation math.
 *        Config is injected through the params fixture; the wall clock is the host's
 *        real time (so it passes the tracker's validity gate). The elevation math has
 *        its own coverage in test_sun.cpp; here we check the wiring: e7->deg
 *        conversion, "compute once valid", and recompute on a geo change.
 */
#include <cstdint>

#include "doctest.h"

#include "params_fixture.h"
#include "libbotanik/sun/sun_tracker.h"
#include "libbotanik/hal/platform.h"

#if BOTANIK_FEATURE_SUN

using namespace Botanik;
using namespace Botanik::Sun;

namespace {
    // Same e7->degrees the tracker does, so a reference solar_position() call
    // gets byte-identical inputs (hence an identical result) to compare against.
    double deg(int32_t e7) { return (double)e7 / 1e7; }
}

TEST_CASE("SunPositionTracker: computes elevation from geo config") {
    botanik_test::ParamFixture fx;
    const int32_t LAT = 488566000;   // Paris 48.8566 N
    const int32_t LON =  23522000;   //       2.3522 E
    fx.set_num("geo", 1);
    fx.set_num("geo_latitude", (float)deg(LAT));
    fx.set_num("geo_longitude", (float)deg(LON));
    fx.publish();

    SunPositionTracker sun(&fx.bank());
    CHECK_FALSE(sun.ready());                 // nothing computed before the first step

    uint64_t now = HAL::Clock::uptime_ms();          // host wall clock is real -> passes the gate
    sun.step(now);
    REQUIRE(sun.ready());

    // Must match a direct call with the same instant + location.
    CHECK(sun.elevation() == doctest::Approx(solar_position(now, deg(LAT), deg(LON)).elevation));
}

TEST_CASE("SunPositionTracker: exposes peak, ratio and rising from the same sample") {
    botanik_test::ParamFixture fx;
    const int32_t LAT = 488566000;   // Paris 48.8566 N
    const int32_t LON =  23522000;   //       2.3522 E
    fx.set_num("geo", 1);
    fx.set_num("geo_latitude", (float)deg(LAT));
    fx.set_num("geo_longitude", (float)deg(LON));
    fx.publish();

    SunPositionTracker sun(&fx.bank());
    uint64_t now = HAL::Clock::uptime_ms();
    sun.step(now);
    REQUIRE(sun.ready());

    // Every accessor must agree with a direct solar_position() at the same instant.
    SolarPosition ref = solar_position(now, deg(LAT), deg(LON));
    CHECK(sun.elevation()        == doctest::Approx(ref.elevation));
    CHECK(sun.peak_elevation()   == doctest::Approx(ref.peak_elevation));
    CHECK(sun.elevation_ratio()  == doctest::Approx(ref.ratio()));
    CHECK(sun.rising()           == ref.rising);
    CHECK(sun.day_length_h()     == doctest::Approx(ref.day_length_h));
    CHECK(sun.max_day_length_h() == doctest::Approx(ref.max_day_length_h));
}

TEST_CASE("SunPositionTracker: throttles recomputation between cadence ticks") {
    botanik_test::ParamFixture fx;
    const int32_t LAT = 488566000;   // Paris
    const int32_t LON =  23522000;
    fx.set_num("geo", 1);
    fx.set_num("geo_latitude", (float)deg(LAT));
    fx.set_num("geo_longitude", (float)deg(LON));
    fx.publish();

    SunPositionTracker sun(&fx.bank());
    uint64_t t0 = HAL::Clock::uptime_ms();
    sun.step(t0);
    REQUIRE(sun.ready());
    double e0 = sun.elevation();

    // A few seconds on (inside the cadence): no recompute, value stays pinned
    // even though the true elevation has moved on.
    sun.step(t0 + 5000);
    CHECK(sun.elevation() == e0);

    // Two minutes on (past the cadence): recomputes for the new instant.
    uint64_t t1 = t0 + 120000;
    sun.step(t1);
    CHECK(sun.elevation() == doctest::Approx(solar_position(t1, deg(LAT), deg(LON)).elevation));
}

TEST_CASE("SunPositionTracker: recomputes for a new location when geo.serial changes") {
    botanik_test::ParamFixture fx;
    fx.set_num("geo", 1);
    fx.set_num("geo_latitude", 48.8566f);   // Paris
    fx.set_num("geo_longitude", 2.3522f);
    fx.publish();

    SunPositionTracker sun(&fx.bank());
    sun.step(HAL::Clock::uptime_ms());
    REQUIRE(sun.ready());
    double paris_elev = sun.elevation();

    // Move far away (Sydney, southern hemisphere) and bump the serial.
    const int32_t LAT = -338700000;   // -33.87
    const int32_t LON = 1512100000;   // 151.21 E
    fx.set_num("geo", 2);
    fx.set_num("geo_latitude", (float)deg(LAT));
    fx.set_num("geo_longitude", (float)deg(LON));
    fx.publish();

    uint64_t now = HAL::Clock::uptime_ms();
    sun.step(now);
    CHECK(sun.elevation() == doctest::Approx(solar_position(now, deg(LAT), deg(LON)).elevation));
    CHECK(sun.elevation() != doctest::Approx(paris_elev));   // different coords -> different angle
}

TEST_CASE("SunPositionTracker: stays idle with no geo config") {
    botanik_test::ParamFixture fx;          // conf all zero -> geo.serial 0
    SunPositionTracker sun(&fx.bank());
    sun.step(HAL::Clock::uptime_ms());
    CHECK_FALSE(sun.ready());
}

#endif // BOTANIK_FEATURE_SUN
