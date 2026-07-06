// REVIEW_35: DONE
/**
 * @file sun/sun_tracker.cpp
 * @brief SunPositionTracker and the NOAA solar-elevation math.
 *
 * Solar-math reference: the standard sunrise-equation formulation
 * (en.wikipedia.org/wiki/Sunrise_equation). Day numbers are counted from J2000
 * (noon-anchored), so the Julian-date arithmetic and the noon-anchored transit
 * fall out directly.
 */
#include <cmath>
#include <cstdint>

#include "libbotanik/hal/platform.h"
#include "libbotanik/utils/time.h"

#include "libbotanik/sun/sun_tracker.h"

#if BOTANIK_FEATURE_SUN

namespace Botanik { namespace Sun {

    // The boot-epoch <-> UNIX conversion lives in Utils (general time helpers).
    using Botanik::Utils::unix_ms_from_uptime_ms;

    // Solar math
    // ========================================================================

    // Constants
    // ------------------------------------------------------------------------

    // Our own PI: M_PI is not guaranteed under strict C++11 (no
    // _USE_MATH_DEFINES), and the Arduino/Mbed core #defines PI as a macro on
    // OPTA, so use a distinct name.
    static const double SOLAR_PI = 3.14159265358979323846;
    static const double DEG2RAD  = SOLAR_PI / 180.0;

    // Earth's mean axial tilt, degrees (the ecliptic obliquity).
    static const double OBLIQUITY_DEG = 23.4397;

    // J2000.0 epoch (2000-01-01 12:00 TT) and the UNIX epoch, both as Julian
    // dates.
    static const double JD_J2000     = 2451545.0;
    static const double JD_UNIX      = 2440587.5;
    static const double MS_PER_DAY   = 86400000.0;

    // Helpers
    // ------------------------------------------------------------------------

    // Sun's ecliptic longitude (degrees) for a count of days @p t since J2000,
    // also returning the solar mean anomaly @p M_deg (its sines feed the
    // transit correction). Both are normalised to [0, 360).
    static double _ecliptic_longitude(double t, double& M_deg) {
        double M = std::fmod(357.5291 + 0.98560028 * t, 360.0);
        if (M < 0.0) M += 360.0;
        double Mr = M * DEG2RAD;

        // equation of the centre (first three harmonics)
        double C = 1.9148 * std::sin(Mr) + 0.0200 * std::sin(2.0 * Mr)
                 + 0.0003 * std::sin(3.0 * Mr);

        // + 282.9372 = 180 (sun vs earth) + 102.9372 (perihelion argument)
        double lambda = std::fmod(M + C + 282.9372, 360.0);
        if (lambda < 0.0) lambda += 360.0;

        M_deg = M;
        return lambda;
    }

    // Daylight hours for a latitude/declination geometry: the sun is up while
    // the hour angle is within +/- H0, where cos(H0) = -tan(lat) tan(decl).
    // Earth turns 2*pi per 24 h, so daylight = 24 * H0 / pi. Saturates to 24 h
    // (polar day) or 0 h (polar night) once the sun no longer crosses the horizon.
    static double _day_length_hours(double sin_lat, double cos_lat,
                                    double sin_decl, double cos_decl) {
        double denom = cos_lat * cos_decl;
        if (denom == 0.0) return 12.0;    // degenerate (pole, decl 0); split the difference
        double cos_H0 = -(sin_lat * sin_decl) / denom;
        if (cos_H0 <= -1.0) return 24.0;  // sun never sets
        if (cos_H0 >=  1.0) return  0.0;  // sun never rises
        return 24.0 * std::acos(cos_H0) / SOLAR_PI;
    }

    // Solar position
    // ------------------------------------------------------------------------

    SolarPosition solar_position(uint64_t now_ms, double lat_deg, double lon_deg) {
        double jd_now = (double)unix_ms_from_uptime_ms(now_ms) / MS_PER_DAY + JD_UNIX;

        // Integer day count since J2000 whose solar noon is nearest "now" (so
        // the hour angle below lands near [-180, 180] deg). lon east +, so an
        // eastern location's noon falls earlier in UTC.
        long n = (long)std::llround(jd_now - JD_J2000 + lon_deg / 360.0);

        // Mean solar time at this longitude, and the sun's ecliptic longitude /
        // mean anomaly for that day.
        double jstar = (double)n - lon_deg / 360.0;
        double M;
        double lambda   = _ecliptic_longitude(jstar, M);
        double Mr       = M * DEG2RAD;
        double lambda_r = lambda * DEG2RAD;

        // Solar transit (Julian date of local solar noon), equation of time
        // folded in.
        double jtransit = JD_J2000 + jstar
                        + 0.0053 * std::sin(Mr) - 0.0069 * std::sin(2.0 * lambda_r);

        // Declination from the ecliptic longitude.
        double sin_decl = std::sin(lambda_r) * std::sin(OBLIQUITY_DEG * DEG2RAD);
        double cos_decl = std::sqrt(1.0 - sin_decl * sin_decl);

        // Hour angle: Earth turns 360 deg per solar day, zero at transit. cos is
        // periodic, so there is no need to wrap it into range.
        double H_r = (jd_now - jtransit) * 360.0 * DEG2RAD;

        double lat_r   = lat_deg * DEG2RAD;
        double sin_lat = std::sin(lat_r);
        double cos_lat = std::cos(lat_r);

        SolarPosition p;
        // Current altitude (elevation) above the horizon.
        double sin_alt = sin_lat * sin_decl + cos_lat * cos_decl * std::cos(H_r);
        p.elevation = std::asin(sin_alt);
        // Peak altitude is reached at solar noon, where the hour angle is 0 (so
        // cos(H) = 1); it reduces to the classic 90 - |lat - decl|. Reported as a
        // magnitude (always >= 0): it only goes negative in deep polar night,
        // where we still want a positive reference for the (signed) elevation ratio.
        double sin_alt_peak = sin_lat * sin_decl + cos_lat * cos_decl;
        p.peak_elevation = std::fabs(std::asin(sin_alt_peak));
        // Before transit the sun is still climbing toward noon; after, falling.
        p.rising = jd_now < jtransit;

        // Today's daylight span from the current declination.
        p.day_length_h = _day_length_hours(sin_lat, cos_lat, sin_decl, cos_decl);
        // Longest day at this latitude: the summer solstice, where the
        // declination reaches the obliquity toward this hemisphere (same sign as
        // the latitude). At the equator this is a flat 12 h either way.
        double sign_lat     = (sin_lat >= 0.0) ? 1.0 : -1.0;
        double sin_decl_max = std::sin(OBLIQUITY_DEG * DEG2RAD) * sign_lat;
        double cos_decl_max = std::cos(OBLIQUITY_DEG * DEG2RAD);
        p.max_day_length_h  = _day_length_hours(sin_lat, cos_lat, sin_decl_max, cos_decl_max);
        return p;
    }

    // SunPositionTracker
    // ========================================================================

    // Wall-clock sanity floor (ms since the UNIX epoch, ~2020-09): below this
    // the RTC is unset (no NTP yet) and the date-dependent solar math is
    // meaningless, so we wait.
    static const uint64_t WALL_CLOCK_VALID_MS = 1600000000000ull;

    SunPositionTracker::SunPositionTracker(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger)
        : Component(logger, "SunPositionTracker"), _ctx(ctx),
          _pos(), _next_ms(0), _computed(false) {}

    uint32_t SunPositionTracker::config_serial() const {
        const PlcIO::PlcOutputConfig_t* c = config();
        return c ? c->geo_serial : 0;
    }

    void SunPositionTracker::on_config_changed(uint64_t now_ms) {
        (void)now_ms;
        // New location: drop the old computation; on_step recomputes once the
        // clock is valid.
        _computed = false;
    }

    void SunPositionTracker::on_step(uint64_t now_ms) {
        const PlcIO::PlcOutputConfig_t* c = config();
        // no location configured
        if (c == nullptr || c->geo_serial == 0) return;
        // wait for a real (NTP) time before the date-dependent solar math
        if (HAL::Clock::wall_ms() < WALL_CLOCK_VALID_MS) return;

        // The elevation drifts slowly, so recompute only on cadence (or on the
        // first valid beat / after a geo change, both signalled by !_computed).
        if (_computed && now_ms < _next_ms) return;

        double lat = (double)c->geo_latitude_e7  / 1e7;
        double lon = (double)c->geo_longitude_e7 / 1e7;
        _pos      = solar_position(now_ms, lat, lon);
        _computed = true;
        _next_ms  = now_ms + Config::SUN_RECOMPUTE_MS;
    }

}}

#endif // BOTANIK_FEATURE_SUN
