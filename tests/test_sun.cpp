/**
 * @file test_sun.cpp
 * @brief Solar position maths and the config-driven tracker over it.
 *
 * Each section below is the former test_<name>.cpp, kept in its own
 * namespace so their fixtures and helpers cannot collide.
 */

#include <cmath>                        // std::abs
#include <cstdint>                      // uint64_t
#include "libbotanik/sun/sun_tracker.h" // Sun::solar_elevation + SolarPosition
#include "libbotanik/utils/time.h"      // Utils
#include "doctest.h"
#include "libbotanik/hal/platform.h"    // HAL::Clock::set_wall_ms
#include "params_fixture.h"

// --------------------------------------------------------------------------
// sun
// --------------------------------------------------------------------------

namespace sun {

#if BOTANIK_FEATURE_SUN_TRACKER

using namespace Botanik::Sun;
using Botanik::Utils::uptime_ms_from_unix_ms;

static const double RAD2DEG = 57.29577951308232;

// Elevation in degrees at a fixed UNIX instant (seconds) for a location.
static double elev_deg(uint64_t unix_s, double lat_deg, double lon_deg) {
    return solar_position(uptime_ms_from_unix_ms(unix_s * 1000), lat_deg, lon_deg).elevation * RAD2DEG;
}

// Full position at a fixed UNIX instant (seconds) for a location.
static SolarPosition pos_at(uint64_t unix_s, double lat_deg, double lon_deg) {
    return solar_position(uptime_ms_from_unix_ms(unix_s * 1000), lat_deg, lon_deg);
}

// solar_elevation: temperate latitudes at solar noon
// ----------------------------------------------------------------------------

TEST_CASE("solar_elevation: London June-solstice noon peaks near the obliquity") {
    // 2024-06-21 12:00 UTC, ~London solar noon. decl ~ +23.44, lat 51.5074 ->
    // 90 - (51.5074 - 23.44) = 61.9 deg.
    double e = elev_deg(1718971200ULL, 51.5074, -0.1278);
    CHECK(std::abs(e - 61.9) < 1.0);
}

TEST_CASE("solar_elevation: London equinox noon sits at ~ (90 - latitude)") {
    // 2024-09-22 12:00 UTC (equinox day, decl ~ 0) -> 90 - 51.5074 = 38.5 deg.
    double e = elev_deg(1726920000ULL, 51.5074, -0.1278);
    CHECK(std::abs(e - 38.5) < 1.0);
}

// solar_elevation: below the horizon
// ----------------------------------------------------------------------------

TEST_CASE("solar_elevation: negative around solar midnight") {
    // 2024-06-21 00:00 UTC, ~London solar midnight: the sun is well below the
    // horizon (~-15 deg).
    double e = elev_deg(1718928000ULL, 51.5074, -0.1278);
    CHECK(e < -5.0);
}

// solar_elevation: southern hemisphere
// ----------------------------------------------------------------------------

TEST_CASE("solar_elevation: Sydney December-solstice noon is high (local summer)") {
    // 2024-12-21 ~01:53 UTC, ~Sydney solar noon. decl ~ -23.44, lat -33.8688 ->
    // 90 - |(-33.8688) - (-23.44)| = 79.6 deg. Near the daily peak, so a few
    // minutes of slack in "solar noon" barely moves it.
    double e = elev_deg(1734746010ULL, -33.8688, 151.2093);
    CHECK(std::abs(e - 79.6) < 1.5);
}

// solar_position: rising / falling
// ----------------------------------------------------------------------------

TEST_CASE("solar_position: rising in the morning, falling in the afternoon") {
    // London, 2024-06-21 (solar noon ~12:00 UTC). Three hours either side is
    // unambiguously before / after the transit.
    CHECK(pos_at(1718960400ULL, 51.5074, -0.1278).rising);        // 09:00 UTC
    CHECK_FALSE(pos_at(1718982000ULL, 51.5074, -0.1278).rising);  // 15:00 UTC
}

// solar_position: peak elevation
// ----------------------------------------------------------------------------

TEST_CASE("solar_position: peak elevation is the day's noon altitude and bounds it") {
    // London June solstice: the noon peak is ~61.9 deg (90 - (51.5074 - 23.44)),
    // and it is a property of the day - the same whether sampled morning or
    // afternoon - while the live elevation stays at or below it.
    SolarPosition am = pos_at(1718960400ULL, 51.5074, -0.1278);   // 09:00 UTC
    SolarPosition pm = pos_at(1718982000ULL, 51.5074, -0.1278);   // 15:00 UTC
    CHECK(std::abs(am.peak_elevation * RAD2DEG - 61.9) < 1.0);
    CHECK(std::abs(pm.peak_elevation * RAD2DEG - 61.9) < 1.0);
    CHECK(am.elevation <= am.peak_elevation);
    CHECK(pm.elevation <= pm.peak_elevation);
}

// solar_position: elevation ratio
// ----------------------------------------------------------------------------

TEST_CASE("solar_position: ratio peaks near 1 at noon and goes negative below the horizon") {
    // At solar noon the live elevation almost equals the day's peak -> ratio ~1.
    SolarPosition noon = pos_at(1718971200ULL, 51.5074, -0.1278);   // ~solar noon
    CHECK(noon.ratio() > 0.99);
    CHECK(noon.ratio() <= 1.0);

    // At solar midnight the sun is below the horizon (elevation < 0) while the
    // day's peak stays positive, so the ratio is negative.
    SolarPosition midnight = pos_at(1718928000ULL, 51.5074, -0.1278);
    CHECK(midnight.ratio() < 0.0);
    CHECK(midnight.elevation < 0.0);   // the angle itself is signed too
}

TEST_CASE("solar_position: ratio stays negative below the horizon even in polar night") {
    // North pole, December solstice: the sun is below the horizon all day. The
    // peak is still reported as a positive magnitude, but the elevation (and so
    // the ratio) must report the sun as down (negative), not fold to zero.
    SolarPosition polar = pos_at(1734782400ULL, 89.9, 0.0);
    CHECK(polar.elevation < 0.0);
    CHECK(polar.peak_elevation > 0.0);
    CHECK(polar.ratio() < 0.0);
}

// solar_position: day length
// ----------------------------------------------------------------------------

TEST_CASE("solar_position: day length tracks the season and bounds by the yearly max") {
    // Geometric daylight (sun's centre crossing a 0-deg horizon, no refraction),
    // so these run ~0.2 h under the refraction-corrected almanac figures.
    // London June solstice: longest day of the year, ~16.4 h, and it equals the
    // latitude's maximum (which is season-independent).
    SolarPosition jun = pos_at(1718971200ULL, 51.5074, -0.1278);
    CHECK(std::abs(jun.day_length_h - 16.4) < 0.2);
    CHECK(jun.day_length_h == doctest::Approx(jun.max_day_length_h).epsilon(0.01));

    // London December solstice (2024-12-21): shortest day, ~7.6 h, well under
    // the yearly max, which is a property of the latitude and stays ~16.4 h.
    SolarPosition dec = pos_at(1734782400ULL, 51.5074, -0.1278);
    CHECK(std::abs(dec.day_length_h - 7.6) < 0.2);
    CHECK(dec.day_length_h < dec.max_day_length_h);
    CHECK(dec.max_day_length_h == doctest::Approx(jun.max_day_length_h).epsilon(0.01));
}

TEST_CASE("solar_position: equator is ~12 h year round, poles saturate") {
    // Equator: ~12 h daylight regardless of season, and the max is ~12 h too.
    SolarPosition eq = pos_at(1718971200ULL, 0.0, 0.0);
    CHECK(std::abs(eq.day_length_h - 12.0) < 0.1);
    CHECK(std::abs(eq.max_day_length_h - 12.0) < 0.1);

    // North pole at the June solstice: continuous polar day (24 h).
    SolarPosition pole = pos_at(1718971200ULL, 89.9, 0.0);
    CHECK(pole.day_length_h == doctest::Approx(24.0));
    // ...and its December counterpart: polar night (0 h).
    SolarPosition pole_dec = pos_at(1734782400ULL, 89.9, 0.0);
    CHECK(pole_dec.day_length_h == doctest::Approx(0.0));
}

#endif // BOTANIK_FEATURE_SUN_TRACKER

}  // namespace sun

// --------------------------------------------------------------------------
// sun_tracker
// --------------------------------------------------------------------------

namespace sun_tracker {

#if BOTANIK_FEATURE_SUN_TRACKER

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
    fx.set_num("geo_serial", 1);
    fx.set_num("geo_lat_deg", (float)deg(LAT));
    fx.set_num("geo_lon_deg", (float)deg(LON));
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
    fx.set_num("geo_serial", 1);
    fx.set_num("geo_lat_deg", (float)deg(LAT));
    fx.set_num("geo_lon_deg", (float)deg(LON));
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
    fx.set_num("geo_serial", 1);
    fx.set_num("geo_lat_deg", (float)deg(LAT));
    fx.set_num("geo_lon_deg", (float)deg(LON));
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
    fx.set_num("geo_serial", 1);
    fx.set_num("geo_lat_deg", 48.8566f);   // Paris
    fx.set_num("geo_lon_deg", 2.3522f);
    fx.publish();

    SunPositionTracker sun(&fx.bank());
    sun.step(HAL::Clock::uptime_ms());
    REQUIRE(sun.ready());
    double paris_elev = sun.elevation();

    // Move far away (Sydney, southern hemisphere) and bump the serial.
    const int32_t LAT = -338700000;   // -33.87
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
