/**
 * @file test_sun.cpp
 * @brief Tests for the sun/sun_tracker.cpp solar math - the sun's elevation
 *        angle above the horizon.
 *
 * Like @c cal_time_ms(), solar_position() folds in a boot epoch sampled from
 * the wall clock, so it is not fixed across runs when fed a raw uptime. But the
 * API round-trips cleanly: a fixed UNIX instant mapped *into* the uptime domain
 * with @c uptime_ms_from_unix_ms() cancels the epoch exactly, so the answers are
 * deterministic and can be pinned against the geometry.
 *
 * At solar noon the hour angle is ~0 and the elevation peaks at 90 - |lat - decl|
 * (its daily maximum, where it is insensitive to a few minutes of clock slack);
 * declination swings to +/-obliquity at the solstices and ~0 at the equinoxes.
 * Latitudes are north +, longitudes east +.
 */
#include <cmath>                      // std::abs - the floating-point tolerance checks
#include <cstdint>                    // uint64_t - the wall-clock timestamps positions are computed for

#include "libbotanik/sun/sun_tracker.h" // Sun::solar_elevation + SolarPosition - under test
#include "libbotanik/utils/time.h"    // Utils - building the test timestamps

#include "doctest.h"                  // the test framework (vendored single header); last so it sees our types

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
