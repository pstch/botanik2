/**
 * @file test_sun.cpp
 * @brief Tests for utils/sun.cpp - solar declination and next sunrise/sunset.
 *
 * Like @c cal_time_ms(), these functions fold in a boot epoch sampled from the
 * wall clock, so an absolute timestamp is not fixed across runs. But the API is
 * a clean round-trip: a fixed UNIX instant mapped *into* the uptime domain with
 * @c uptime_ms_from_unix_ms() and the results mapped back *out* with
 * @c unix_ms_from_uptime_ms() cancel the epoch exactly, so the UNIX-time answers
 * are deterministic and can be pinned against an almanac.
 *
 * Reference values are public almanac sunrise/sunset (NOAA), good to the minute;
 * the ~1-minute tolerance below covers the algorithm's approximation, not run
 * variance. Latitudes are north +, longitudes east +.
 */
#include <cstdint>
#include <cstdlib>   // std::abs (integer)
#include <cmath>     // std::abs (floating point)

#include "doctest.h"

#include "libbotanik/utils/sun.h"
#include "libbotanik/utils/time.h"

using namespace Botanik::Utils;

// 2024-06-21 00:00:00 UTC (June solstice) and 2024-12-21 12:00:00 UTC (December
// solstice), as UNIX seconds.
static const uint64_t SOLSTICE_JUN = 1718928000ULL;
static const uint64_t SOLSTICE_DEC = 1734782400ULL;

// UNIX seconds of an event, recovered from its uptime-domain timestamp.
static int64_t event_unix_s(SunEvent e) {
    return (int64_t)(unix_ms_from_uptime_ms(e.time_ms) / 1000);
}

// Seconds-of-day (UTC) of an event.
static int event_utc_secs_of_day(SunEvent e) {
    return (int)(event_unix_s(e) % 86400);
}

#define HHMM(h, m) ((int)((h) * 3600 + (m) * 60))
static const int TOL_S = 90;  // ~1.5 min: the approximation's worst-case slack

// sun_declination
// ----------------------------------------------------------------------------

TEST_CASE("sun_declination: peaks at the obliquity on the solstices") {
    const double RAD2DEG = 57.29577951308232;
    // June solstice: maximally north (+23.44 deg); December: maximally south.
    CHECK(sun_declination(uptime_ms_from_unix_ms(SOLSTICE_JUN * 1000)) * RAD2DEG
          == doctest::Approx(23.44).epsilon(0.01));
    CHECK(sun_declination(uptime_ms_from_unix_ms(SOLSTICE_DEC * 1000)) * RAD2DEG
          == doctest::Approx(-23.44).epsilon(0.01));
}

TEST_CASE("sun_declination: near zero at an equinox") {
    // 2024-09-22 12:44 UTC, autumnal equinox. Compare with an absolute bound: a
    // relative Approx collapses to ~zero tolerance against a zero target.
    uint64_t eq = uptime_ms_from_unix_ms(1726922640ULL * 1000);
    CHECK(std::abs(sun_declination(eq) * 57.29577951308232) < 1.0);  // within 1 deg of 0
}

// next_sun_times: temperate latitudes
// ----------------------------------------------------------------------------

TEST_CASE("next_sun_times: London on the June solstice matches the almanac") {
    // London, 51.5074 N, 0.1278 W. Almanac (UTC): sunrise 03:43, sunset 20:21.
    uint64_t now = uptime_ms_from_unix_ms(SOLSTICE_JUN * 1000);
    SunTimes t = next_sun_times(now, 51.5074, -0.1278);

    REQUIRE(t.sunrise.valid);
    REQUIRE(t.sunset.valid);
    CHECK(std::abs(event_utc_secs_of_day(t.sunrise) - HHMM(3, 43)) <= TOL_S);
    CHECK(std::abs(event_utc_secs_of_day(t.sunset)  - HHMM(20, 21)) <= TOL_S);
}

TEST_CASE("next_sun_times: both events are strictly in the future") {
    uint64_t now = uptime_ms_from_unix_ms(SOLSTICE_JUN * 1000);
    SunTimes t = next_sun_times(now, 51.5074, -0.1278);
    REQUIRE(t.sunrise.valid);
    REQUIRE(t.sunset.valid);
    CHECK(t.sunrise.time_ms > now);
    CHECK(t.sunset.time_ms  > now);
}

TEST_CASE("next_sun_times: sunrise and sunset resolve independently across dusk") {
    // Reference instant just after London's solstice sunset (20:30 UTC): the next
    // sunset is the following day, but the next sunrise is still the next morning,
    // so sunrise must come before sunset here.
    uint64_t now = uptime_ms_from_unix_ms((SOLSTICE_JUN + HHMM(20, 30)) * 1000);
    SunTimes t = next_sun_times(now, 51.5074, -0.1278);
    REQUIRE(t.sunrise.valid);
    REQUIRE(t.sunset.valid);
    CHECK(t.sunrise.time_ms < t.sunset.time_ms);
    // next sunrise is ~03:43 the following morning
    CHECK(std::abs(event_utc_secs_of_day(t.sunrise) - HHMM(3, 43)) <= TOL_S);
}

TEST_CASE("next_sun_times: southern hemisphere (Sydney) in its winter") {
    // Sydney, 33.8688 S, 151.2093 E. June solstice = local winter; almanac (UTC):
    // sunset 06:53, sunrise 21:00 (07:00 / 16:54 AEST = UTC+10).
    uint64_t now = uptime_ms_from_unix_ms(SOLSTICE_JUN * 1000);
    SunTimes t = next_sun_times(now, -33.8688, 151.2093);
    REQUIRE(t.sunrise.valid);
    REQUIRE(t.sunset.valid);
    CHECK(std::abs(event_utc_secs_of_day(t.sunset)  - HHMM(6, 53)) <= TOL_S);
    CHECK(std::abs(event_utc_secs_of_day(t.sunrise) - HHMM(21, 0)) <= TOL_S);
}

// next_sun_times: polar behaviour
// ----------------------------------------------------------------------------

TEST_CASE("next_sun_times: polar night still resolves the next real sunrise") {
    // Tromso, 69.65 N, on the December solstice is mid polar night: no sunrise for
    // weeks. The forward search must skip the blank days and land on mid-January,
    // not give up - so the event is valid and lies well in the future.
    uint64_t now = uptime_ms_from_unix_ms(SOLSTICE_DEC * 1000);
    SunTimes t = next_sun_times(now, 69.6492, 18.9553);
    REQUIRE(t.sunrise.valid);
    // first sunrise after polar night is ~3-4 weeks out
    int64_t days_ahead = (event_unix_s(t.sunrise) - (int64_t)SOLSTICE_DEC) / 86400;
    CHECK(days_ahead >= 18);
    CHECK(days_ahead <= 35);
}
