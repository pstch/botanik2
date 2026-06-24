/**
 * @file test_sun_tracker.cpp
 * @brief Tests for SunPositionTracker - the geo-config-driven wrapper over sun/sun.h.
 *        Config is injected through the PlcConfigFixture; the wall clock is the host's
 *        real time (so it passes the tracker's validity gate). The underlying solar
 *        math has its own coverage in test_sun.cpp; here we check the wiring: e7->deg
 *        conversion, "compute once valid", recompute on a geo change, and day/night.
 */
#include <cstdint>

#include "doctest.h"

#include "plc_config_fixture.h"
#include "libbotanik/sun/sun_tracker.h"
#include "libbotanik/sun/sun.h"
#include "libbotanik/hal/platform.h"

#if BOTANIK_FEATURE_SUN

using namespace Botanik;
using namespace Botanik::Sun;

namespace {
    // Same e7->degrees the tracker does, so a reference next_sun_times() call gets
    // byte-identical inputs (hence identical results) to compare against.
    double deg(int32_t e7) { return (double)e7 / 1e7; }
}

TEST_CASE("SunPositionTracker: computes from geo config, matching utils/sun") {
    botanik_test::PlcConfigFixture fx;
    const int32_t LAT = 488566000;   // Paris 48.8566 N
    const int32_t LON =  23522000;   //       2.3522 E
    fx.conf().geo.serial       = 1;
    fx.conf().geo.latitude_e7  = LAT;
    fx.conf().geo.longitude_e7 = LON;
    fx.publish();

    SunPositionTracker sun(fx.ctx());
    CHECK_FALSE(sun.ready());                 // nothing computed before the first step

    uint64_t now = HAL::Clock::uptime_ms();          // host wall clock is real -> passes the gate
    sun.step(now);
    REQUIRE(sun.ready());

    // Must match a direct call with the same instant + location.
    SunTimes ref = next_sun_times(now, deg(LAT), deg(LON));
    CHECK(sun.times().sunrise.valid == ref.sunrise.valid);
    CHECK(sun.times().sunset.valid  == ref.sunset.valid);
    if (ref.sunrise.valid) CHECK(sun.times().sunrise.time_ms == ref.sunrise.time_ms);
    if (ref.sunset.valid)  CHECK(sun.times().sunset.time_ms  == ref.sunset.time_ms);

    // Daytime iff the sun sets before it next rises.
    if (ref.sunrise.valid && ref.sunset.valid)
        CHECK(sun.is_daytime() == (ref.sunset.time_ms < ref.sunrise.time_ms));
}

TEST_CASE("SunPositionTracker: recomputes for a new location when geo.serial changes") {
    botanik_test::PlcConfigFixture fx;
    fx.conf().geo.serial = 1;
    fx.conf().geo.latitude_e7  = 488566000;   // Paris
    fx.conf().geo.longitude_e7 =  23522000;
    fx.publish();

    SunPositionTracker sun(fx.ctx());
    sun.step(HAL::Clock::uptime_ms());
    REQUIRE(sun.ready());
    uint64_t paris_sunrise = sun.times().sunrise.time_ms;

    // Move far away (Sydney, southern hemisphere) and bump the serial.
    const int32_t LAT = -338700000;   // -33.87
    const int32_t LON = 1512100000;   // 151.21 E
    fx.conf().geo.serial       = 2;
    fx.conf().geo.latitude_e7  = LAT;
    fx.conf().geo.longitude_e7 = LON;
    fx.publish();

    uint64_t now = HAL::Clock::uptime_ms();
    sun.step(now);
    SunTimes ref = next_sun_times(now, deg(LAT), deg(LON));
    if (ref.sunrise.valid) {
        CHECK(sun.times().sunrise.time_ms == ref.sunrise.time_ms);   // picked up the new coords
        CHECK(sun.times().sunrise.time_ms != paris_sunrise);         // and they really differ
    }
}

TEST_CASE("SunPositionTracker: stays idle with no geo config") {
    botanik_test::PlcConfigFixture fx;          // conf all zero -> geo.serial 0
    SunPositionTracker sun(fx.ctx());
    sun.step(HAL::Clock::uptime_ms());
    CHECK_FALSE(sun.ready());
}

#endif // BOTANIK_FEATURE_SUN
