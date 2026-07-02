/**
 * @file test_sun.cpp
 * @brief Tests for the sun/sun_tracker.cpp solar math - the sun's elevation
 *        angle above the horizon.
 *
 * Like @c cal_time_ms(), solar_elevation() folds in a boot epoch sampled from
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
#include <cstdint>
#include <cmath>     // std::abs (floating point)

#include "doctest.h"

#include "libbotanik/sun/sun_tracker.h"
#include "libbotanik/utils/time.h"

#if BOTANIK_FEATURE_SUN

using namespace Botanik::Sun;
using Botanik::Utils::uptime_ms_from_unix_ms;

static const double RAD2DEG = 57.29577951308232;

// Elevation in degrees at a fixed UNIX instant (seconds) for a location.
static double elev_deg(uint64_t unix_s, double lat_deg, double lon_deg) {
    return solar_elevation(uptime_ms_from_unix_ms(unix_s * 1000), lat_deg, lon_deg) * RAD2DEG;
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

#endif // BOTANIK_FEATURE_SUN
