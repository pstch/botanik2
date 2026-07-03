// REVIEW_35: TODO
/**
 * @file sun/sun_tracker.cpp
 * @brief SunPositionTracker and the NOAA solar-elevation math.
 *
 * Solar-math reference: the standard sunrise-equation formulation
 * (en.wikipedia.org/wiki/Sunrise_equation). Day numbers are counted from J2000
 * (noon-anchored), so the Julian-date arithmetic and the noon-anchored transit
 * fall out directly.
 */

#include "libbotanik/sun/sun_tracker.h"

#if BOTANIK_FEATURE_SUN

#include <cmath>
#include <cstdint>

#include "libbotanik/hal/platform.h"   // HAL::Clock::wall_ms
#include "libbotanik/utils/time.h"     // uptime <-> unix ms conversions

namespace Botanik { namespace Sun {

    // The boot-epoch <-> UNIX conversion lives in Utils (general time helpers).
    using Botanik::Utils::unix_ms_from_uptime_ms;

    // Solar math
    // ========================================================================

    // Constants
    // ------------------------------------------------------------------------

    // Our own PI: M_PI is not guaranteed under strict C++11 (no
    // _USE_MATH_DEFINES).
    static const double PI       = 3.14159265358979323846;
    static const double DEG2RAD  = PI / 180.0;

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

    // Solar position
    // ------------------------------------------------------------------------

    double solar_elevation(uint64_t now_ms, double lat_deg, double lon_deg) {
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

        // Altitude (elevation) above the horizon.
        double lat_r = lat_deg * DEG2RAD;
        double sin_alt = std::sin(lat_r) * sin_decl
                       + std::cos(lat_r) * cos_decl * std::cos(H_r);
        return std::asin(sin_alt);
    }

    // SunPositionTracker
    // ========================================================================

    // Wall-clock sanity floor (ms since the UNIX epoch, ~2020-09): below this
    // the RTC is unset (no NTP yet) and the date-dependent solar math is
    // meaningless, so we wait.
    static const uint64_t WALL_CLOCK_VALID_MS = 1600000000000ull;

    // Recompute cadence (ms): the elevation drifts ~0.25 deg/min, so a minute
    // between updates keeps it well under a degree stale for a fraction of the
    // per-beat trig work.
    static const uint64_t SUN_RECOMPUTE_MS = 60000ull;

    SunPositionTracker::SunPositionTracker(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger)
        : ConfigHandler(ctx, logger, "SunPositionTracker"),
          _elevation(0.0), _next_ms(0), _computed(false) {}

    uint32_t SunPositionTracker::config_serial() const {
        const PlcIO::PlcOutputConfig_t* c = config();
        return c ? c->geo.serial : 0;
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
        if (c == nullptr || c->geo.serial == 0) return;
        // wait for a real (NTP) time before the date-dependent solar math
        if (HAL::Clock::wall_ms() < WALL_CLOCK_VALID_MS) return;

        // The elevation drifts slowly, so recompute only on cadence (or on the
        // first valid beat / after a geo change, both signalled by !_computed).
        if (_computed && now_ms < _next_ms) return;

        double lat = (double)c->geo.latitude_e7  / 1e7;
        double lon = (double)c->geo.longitude_e7 / 1e7;
        _elevation = solar_elevation(now_ms, lat, lon);
        _computed  = true;
        _next_ms   = now_ms + SUN_RECOMPUTE_MS;
    }

}}

#endif // BOTANIK_FEATURE_SUN
