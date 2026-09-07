/**
 * @file test_sun.cpp
 * @brief The solar-position math against instants whose answers are known
 *        independently: the two solstices and an equinox, at three latitudes.
 */

#include "doctest.h"

#include "libbotanik.h"

#if BOTANIK_FEATURE_SUN_TRACKER

#include "libbotanik/sun/sun_tracker.h"

using namespace Botanik;

namespace {

// solar_position() takes an uptime and converts it to a wall instant itself,
// so an instant goes in through that conversion's inverse. The sim's wall
// clock is the host's and cannot be set.
Sun::SolarPosition at(uint64_t unix_ms, double lat, double lon) {
    return Sun::solar_position(Utils::uptime_ms_from_unix_ms(unix_ms), lat, lon);
}

const double RAD2DEG = 57.29577951308232;

// 2026 instants, UTC midnight, from the civil calendar.
const uint64_t JUN21 = 1782000000000ull;   // 2026-06-21 00:00 UTC
const uint64_t DEC21 = 1797811200000ull;   // 2026-12-21 00:00 UTC
const uint64_t MAR20 = 1773964800000ull;   // 2026-03-20 00:00 UTC

}  // namespace

TEST_CASE("sun: day length at the solstices brackets the equinox") {
    // Paris. The almanac gives ~16h10m on the June solstice, ~8h15m in
    // December and ~12h either side of the March equinox. The model is a
    // centre-of-disc one with no refraction, so it reads a few minutes short
    // of a published sunrise/sunset table - the bracket is what is checked.
    const double lat = 48.8566, lon = 2.3522;

    const double jun = at(JUN21, lat, lon).day_length_h;
    const double dec = at(DEC21, lat, lon).day_length_h;
    const double mar = at(MAR20, lat, lon).day_length_h;

    CHECK(jun == doctest::Approx(16.10).epsilon(0.02));
    CHECK(dec == doctest::Approx(8.15).epsilon(0.02));
    CHECK(mar == doctest::Approx(12.10).epsilon(0.02));
    CHECK(jun > mar);
    CHECK(mar > dec);
}

TEST_CASE("sun: peak elevation is 90 minus the latitude/declination gap") {
    // The closed form the geometry reduces to at solar noon. Checked at a
    // latitude far from the declination, so the sign of the gap is unambiguous.
    const double lat = 48.8566, lon = 2.3522;

    const double jun = at(JUN21, lat, lon).peak_elevation * RAD2DEG;
    const double dec = at(DEC21, lat, lon).peak_elevation * RAD2DEG;

    CHECK(jun == doctest::Approx(90.0 - (lat - 23.44)).epsilon(0.002));
    CHECK(dec == doctest::Approx(90.0 - (lat + 23.44)).epsilon(0.002));
}

TEST_CASE("sun: solar noon lands near local mean noon, offset by longitude") {
    // Solar noon at longitude L is 12:00 UTC minus L/15 hours, plus the
    // equation of time (never more than ~17 min). East is earlier.
    const uint64_t DAY = 86400000ull;

    const Sun::SolarPosition east = at(MAR20, 48.8566, 15.0);
    const Sun::SolarPosition west = at(MAR20, 48.8566, -15.0);

    const double east_h = (double)(east.transit_ms % DAY) / 3600000.0;
    const double west_h = (double)(west.transit_ms % DAY) / 3600000.0;

    CHECK(east_h == doctest::Approx(11.0).epsilon(0.03));
    CHECK(west_h == doctest::Approx(13.0).epsilon(0.03));

    INFO("sunrise and sunset straddle the transit by half the day length "
         "(each end truncates its own half, so the span can be 1 ms short)");
    const uint64_t span = east.sunset_ms() - east.sunrise_ms();
    const uint64_t want = (uint64_t)(east.day_length_h * 3600000.0);
    CHECK(want - span <= 1u);
}

TEST_CASE("sun: elevation tracks the hour angle across a day") {
    // Midnight is the deepest point and noon the highest, at a latitude where
    // both are unambiguous.
    const double lat = 48.8566, lon = 0.0;
    const uint64_t H = 3600000ull;

    const double midnight = at(JUN21, lat, lon).elevation * RAD2DEG;
    const double noon     = at(JUN21 + 12 * H, lat, lon).elevation * RAD2DEG;
    const double morning  = at(JUN21 +  9 * H, lat, lon).elevation * RAD2DEG;

    CHECK(midnight < 0.0);
    CHECK(noon > 60.0);
    CHECK(morning > 0.0);
    CHECK(morning < noon);

    INFO("rising is true before the transit and false after");
    CHECK(at(JUN21 + 9 * H, lat, lon).rising);
    CHECK_FALSE(at(JUN21 + 15 * H, lat, lon).rising);
}

TEST_CASE("sun: the polar cases saturate rather than going undefined") {
    const double lon = 0.0;

    INFO("above the arctic circle the June sun never sets");
    CHECK(at(JUN21, 78.0, lon).day_length_h == doctest::Approx(24.0));
    INFO("...and the December sun never rises");
    CHECK(at(DEC21, 78.0, lon).day_length_h == doctest::Approx(0.0));

    INFO("the equator is a flat 12 h all year");
    CHECK(at(JUN21, 0.0, lon).day_length_h == doctest::Approx(12.0).epsilon(0.01));
    CHECK(at(DEC21, 0.0, lon).day_length_h == doctest::Approx(12.0).epsilon(0.01));
}

#endif // BOTANIK_FEATURE_SUN_TRACKER
