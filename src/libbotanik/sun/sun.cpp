/**
 * @file sun/sun.cpp
 * @brief Solar position implementation - NOAA "sunrise equation" approximation.
 *
 * Reference: the standard sunrise-equation formulation (en.wikipedia.org/wiki/
 * Sunrise_equation). Day numbers are counted from J2000 (noon-anchored), so the
 * Julian-date arithmetic and the noon-anchored transit fall out directly.
 */
#include "libbotanik/sun/sun.h"

#if BOTANIK_FEATURE_SUN

#include <cmath>
#include <cstdint>

#include "libbotanik/utils/time.h"   // uptime <-> unix ms conversions

namespace Botanik { namespace Sun {

    // The boot-epoch <-> UNIX conversions live in Utils (general time helpers).
    using Botanik::Utils::unix_ms_from_uptime_ms;
    using Botanik::Utils::uptime_ms_from_unix_ms;

    // Constants
    // ------------------------------------------------------------------------

    // Our own PI: M_PI is not guaranteed under strict C++11 (no
    // _USE_MATH_DEFINES).
    static const double PI       = 3.14159265358979323846;
    static const double DEG2RAD  = PI / 180.0;

    // Earth's mean axial tilt, degrees (the ecliptic obliquity).
    static const double OBLIQUITY_DEG = 23.4397;

    // Sun-centre altitude at the moment of sunrise/sunset, degrees: the
    // standard -50' allowance for atmospheric refraction plus the sun's
    // apparent radius.
    static const double SUN_ALTITUDE_DEG = -0.833;

    // J2000.0 epoch (2000-01-01 12:00 TT) and the UNIX epoch, both as Julian
    // dates.
    static const double JD_J2000     = 2451545.0;
    static const double JD_UNIX      = 2440587.5;
    static const double MS_PER_DAY   = 86400000.0;

    // Small leap-second fudge from the reference algorithm.
    static const double LEAP_FUDGE   = 0.0008;

    // Forward search span for next_sun_times(): a hair over a year, so a polar
    // winter/summer (months with no sunrise or no sunset) still resolves the
    // next real event rather than giving up after one blank day.
    static const int SEARCH_HORIZON_DAYS = 367;

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

    // Convert a Julian date to an absolute uptime-domain ms timestamp.
    static uint64_t _jd_to_uptime_ms(double jd) {
        double unix_ms = (jd - JD_UNIX) * MS_PER_DAY;
        return uptime_ms_from_unix_ms((uint64_t)std::llround(unix_ms));
    }

    // One day's sunrise/sunset as Julian dates. @p n is the integer day count
    // since J2000 (noon-anchored). Returns false (no event) when the sun stays
    // entirely above or below the sunrise altitude that day (polar day/night).
    static bool _day_events(long n, double lat_deg, double lon_deg,
                            double& jrise, double& jset) {
        // mean solar time at this longitude (east +, so noon shifts earlier in
        // UTC)
        double jstar = (double)n - lon_deg / 360.0;

        double M;
        double lambda = _ecliptic_longitude(jstar, M);
        double Mr = M * DEG2RAD;
        double lambda_r = lambda * DEG2RAD;

        // solar transit (Julian date of local solar noon), equation of time
        // folded in
        double jtransit = JD_J2000 + jstar
                        + 0.0053 * std::sin(Mr) - 0.0069 * std::sin(2.0 * lambda_r);

        // declination from the ecliptic longitude
        double sin_decl = std::sin(lambda_r) * std::sin(OBLIQUITY_DEG * DEG2RAD);
        double cos_decl = std::sqrt(1.0 - sin_decl * sin_decl);

        // hour angle of sunrise/sunset; |cos| > 1 means it never reaches that
        // altitude
        double lat_r = lat_deg * DEG2RAD;
        double cos_w = (std::sin(SUN_ALTITUDE_DEG * DEG2RAD) - std::sin(lat_r) * sin_decl)
                     / (std::cos(lat_r) * cos_decl);
        if (cos_w < -1.0 || cos_w > 1.0) return false;

        // half the daylight arc, in days
        double w_days = (std::acos(cos_w) / DEG2RAD) / 360.0;
        jrise = jtransit - w_days;
        jset  = jtransit + w_days;
        return true;
    }

    // Solar position
    // ------------------------------------------------------------------------

    double sun_declination(uint64_t now_ms) {
        double jd = (double)unix_ms_from_uptime_ms(now_ms) / MS_PER_DAY + JD_UNIX;
        double t  = jd - JD_J2000 + LEAP_FUDGE;

        double M;
        double lambda_r = _ecliptic_longitude(t, M) * DEG2RAD;
        return std::asin(std::sin(lambda_r) * std::sin(OBLIQUITY_DEG * DEG2RAD));
    }

    SunTimes next_sun_times(uint64_t now_ms, double lat_deg, double lon_deg) {
        SunTimes out;
        out.sunrise.time_ms = 0; out.sunrise.valid = false;
        out.sunset.time_ms  = 0; out.sunset.valid  = false;

        // day count since J2000 for "now" (noon-anchored, like the algorithm's
        // n)
        double jd_now = (double)unix_ms_from_uptime_ms(now_ms) / MS_PER_DAY + JD_UNIX;
        long n0 = (long)std::floor(jd_now - JD_J2000 + LEAP_FUDGE);

        // Walk forward day by day, taking the first rise / set that lands after
        // now. Start one day early so a just-past-midnight "now" can't skip
        // today's event.
        for (int i = -1; i <= SEARCH_HORIZON_DAYS; ++i) {
            if (out.sunrise.valid && out.sunset.valid) break;

            double jrise, jset;
            if (!_day_events(n0 + i, lat_deg, lon_deg, jrise, jset)) continue;

            if (!out.sunrise.valid) {
                uint64_t t = _jd_to_uptime_ms(jrise);
                if (t > now_ms) { out.sunrise.time_ms = t; out.sunrise.valid = true; }
            }
            if (!out.sunset.valid) {
                uint64_t t = _jd_to_uptime_ms(jset);
                if (t > now_ms) { out.sunset.time_ms = t; out.sunset.valid = true; }
            }
        }
        return out;
    }

}}

#endif // BOTANIK_FEATURE_SUN
