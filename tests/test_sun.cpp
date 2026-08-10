/**
 * @file test_sun.cpp
 * @brief Solar position maths and the config-driven tracker over it.
 *
 * Two sections, each in its own namespace to avoid fixture collisions.
 */

#include <cmath>                        // std::abs
#include <cstdint>                      // uint64_t
#include "libbotanik/sun/sun_tracker.h" // Sun::solar_elevation + SolarPosition
#include "libbotanik/utils/time.h"      // Utils
#include "doctest.h"
#include "libbotanik/hal/platform.h"    // HAL::Clock::set_wall_ms
#include "params_fixture.h"

// sun

namespace sun {

#if BOTANIK_FEATURE_SUN_TRACKER

using namespace Botanik::Sun;
using Botanik::Utils::uptime_ms_from_unix_ms;

static const double RAD2DEG = 57.29577951308232;

static double elev_deg(uint64_t unix_s, double lat_deg, double lon_deg) {  // elevation, degrees, at a UNIX instant
    return solar_position(uptime_ms_from_unix_ms(unix_s * 1000), lat_deg, lon_deg).elevation * RAD2DEG;
}

static SolarPosition pos_at(uint64_t unix_s, double lat_deg, double lon_deg) {  // full position, same terms
    return solar_position(uptime_ms_from_unix_ms(unix_s * 1000), lat_deg, lon_deg);
}

TEST_CASE("solar_elevation: London June-solstice noon peaks near the obliquity") {
    // decl +23.44, lat 51.5074 -> 90 - (51.5074 - 23.44) = 61.9 deg.
    double e = elev_deg(1718971200ULL, 51.5074, -0.1278);
    CHECK(std::abs(e - 61.9) < 1.0);
}

TEST_CASE("solar_elevation: negative around solar midnight") {
    double e = elev_deg(1718928000ULL, 51.5074, -0.1278);  // London, ~-15 deg
    CHECK(e < -5.0);
}

TEST_CASE("solar_elevation: Sydney December-solstice noon is high (local summer)") {
    // decl -23.44, lat -33.8688 -> 90 - |(-33.8688)-(-23.44)| = 79.6 deg; near
    // the daily peak, so slack in "solar noon" barely moves it.
    double e = elev_deg(1734746010ULL, -33.8688, 151.2093);
    CHECK(std::abs(e - 79.6) < 1.5);
}

TEST_CASE("solar_position: rising in the morning, falling in the afternoon") {
    CHECK(pos_at(1718960400ULL, 51.5074, -0.1278).rising);        // 09:00 UTC, London
    CHECK_FALSE(pos_at(1718982000ULL, 51.5074, -0.1278).rising);  // 15:00 UTC
}

TEST_CASE("solar_position: peak elevation is the day's noon altitude and bounds it") {
    // Noon peak ~61.9 deg (90 - (51.5074 - 23.44)), same AM or PM; live elevation never exceeds it.
    SolarPosition am = pos_at(1718960400ULL, 51.5074, -0.1278);   // 09:00 UTC
    SolarPosition pm = pos_at(1718982000ULL, 51.5074, -0.1278);   // 15:00 UTC
    CHECK(std::abs(am.peak_elevation * RAD2DEG - 61.9) < 1.0);
    CHECK(std::abs(pm.peak_elevation * RAD2DEG - 61.9) < 1.0);
    CHECK(am.elevation <= am.peak_elevation);
    CHECK(pm.elevation <= pm.peak_elevation);
}

TEST_CASE("solar_position: ratio peaks near 1 at noon and goes negative below the horizon") {
    SolarPosition noon = pos_at(1718971200ULL, 51.5074, -0.1278);   // ~solar noon -> ratio ~1
    CHECK(noon.ratio() > 0.99);
    CHECK(noon.ratio() <= 1.0);

    SolarPosition midnight = pos_at(1718928000ULL, 51.5074, -0.1278);  // elevation < 0, peak stays positive
    CHECK(midnight.ratio() < 0.0);
    CHECK(midnight.elevation < 0.0);   // the angle itself is signed too
}

TEST_CASE("solar_position: ratio stays negative below the horizon even in polar night") {
    // North pole, December solstice: sun down all day; peak still reports a
    // positive magnitude, but elevation/ratio must go negative, not fold to zero.
    SolarPosition polar = pos_at(1734782400ULL, 89.9, 0.0);
    CHECK(polar.elevation < 0.0);
    CHECK(polar.peak_elevation > 0.0);
    CHECK(polar.ratio() < 0.0);
}

TEST_CASE("solar_position: day length tracks the season and bounds by the yearly max") {
    // Geometric daylight (no refraction) runs ~0.2 h under almanac figures, hence the epsilon.
    SolarPosition jun = pos_at(1718971200ULL, 51.5074, -0.1278);   // London June: longest day, ~16.4 h
    CHECK(std::abs(jun.day_length_h - 16.4) < 0.2);
    CHECK(jun.day_length_h == doctest::Approx(jun.max_day_length_h).epsilon(0.01));

    SolarPosition dec = pos_at(1734782400ULL, 51.5074, -0.1278);   // December: shortest day, ~7.6 h
    CHECK(std::abs(dec.day_length_h - 7.6) < 0.2);
    CHECK(dec.day_length_h < dec.max_day_length_h);
    CHECK(dec.max_day_length_h == doctest::Approx(jun.max_day_length_h).epsilon(0.01));
}

TEST_CASE("solar_position: equator is ~12 h year round, poles saturate") {
    SolarPosition eq = pos_at(1718971200ULL, 0.0, 0.0);
    CHECK(std::abs(eq.day_length_h - 12.0) < 0.1);
    CHECK(std::abs(eq.max_day_length_h - 12.0) < 0.1);

    SolarPosition pole = pos_at(1718971200ULL, 89.9, 0.0);   // June: continuous polar day
    CHECK(pole.day_length_h == doctest::Approx(24.0));
    SolarPosition pole_dec = pos_at(1734782400ULL, 89.9, 0.0);   // December: polar night
    CHECK(pole_dec.day_length_h == doctest::Approx(0.0));
}

#endif // BOTANIK_FEATURE_SUN_TRACKER

}  // namespace sun

// sun_tracker

namespace sun_tracker {

#if BOTANIK_FEATURE_SUN_TRACKER

using namespace Botanik;
using namespace Botanik::Sun;

namespace {
    double deg(int32_t e7) { return (double)e7 / 1e7; }  // same e7->degrees the tracker uses
}

TEST_CASE("SunPositionTracker: computes elevation from geo config") {
    botanik_test::ParamFixture fx;
    const int32_t LAT = 488566000;   // Paris 48.8566 N
    const int32_t LON =  23522000;   //       2.3522 E
    fx.set_num("geo_serial", 1);
    fx.set_num("geo_lat_deg", (float)deg(LAT));
    fx.set_num("geo_lon_deg", (float)deg(LON));
    fx.publish();

    SunPositionTracker sun(&fx.bank());
    CHECK_FALSE(sun.ready());                 // nothing computed before the first step

    uint64_t now = HAL::Clock::uptime_ms();
    sun.step(now);
    REQUIRE(sun.ready());
    CHECK(sun.elevation() == doctest::Approx(solar_position(now, deg(LAT), deg(LON)).elevation));
}

TEST_CASE("SunPositionTracker: throttles recomputation between cadence ticks") {
    botanik_test::ParamFixture fx;
    const int32_t LAT = 488566000;   // Paris
    const int32_t LON =  23522000;
    fx.set_num("geo_serial", 1);
    fx.set_num("geo_lat_deg", (float)deg(LAT));
    fx.set_num("geo_lon_deg", (float)deg(LON));
    fx.publish();

    SunPositionTracker sun(&fx.bank());
    uint64_t t0 = HAL::Clock::uptime_ms();
    sun.step(t0);
    REQUIRE(sun.ready());
    double e0 = sun.elevation();

    sun.step(t0 + 5000);                      // inside cadence: no recompute, value stays pinned
    CHECK(sun.elevation() == e0);

    uint64_t t1 = t0 + 120000;                // past cadence: recomputes
    sun.step(t1);
    CHECK(sun.elevation() == doctest::Approx(solar_position(t1, deg(LAT), deg(LON)).elevation));
}

TEST_CASE("SunPositionTracker: recomputes for a new location when geo.serial changes") {
    botanik_test::ParamFixture fx;
    fx.set_num("geo_serial", 1);
    fx.set_num("geo_lat_deg", 48.8566f);   // Paris
    fx.set_num("geo_lon_deg", 2.3522f);
    fx.publish();

    SunPositionTracker sun(&fx.bank());
    sun.step(HAL::Clock::uptime_ms());
    REQUIRE(sun.ready());
    double paris_elev = sun.elevation();

    const int32_t LAT = -338700000;   // Sydney; -33.87
    const int32_t LON = 1512100000;   // 151.21 E
    fx.set_num("geo_serial", 2);
    fx.set_num("geo_lat_deg", (float)deg(LAT));
    fx.set_num("geo_lon_deg", (float)deg(LON));
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

#endif // BOTANIK_FEATURE_SUN_TRACKER

}  // namespace sun_tracker
