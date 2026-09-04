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

#include <cmath>
#include <cstdint>

#include "libbotanik/config/sun.h"

#include "libbotanik/hal/platform.h"
#include "libbotanik/utils/time.h"

#if BOTANIK_FEATURE_SUN_TRACKER

namespace Botanik { namespace Sun {

    // The boot-epoch <-> UNIX conversion lives in Utils (general time helpers).
    using Botanik::Utils::unix_ms_from_uptime_ms;

    // Solar math

    // Constants

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

    // The trigonometry is single precision. Every argument below is an angle of
    // a few radians and every result is a sine or an angle read out in degrees,
    // where the float kernels' ~1e-7 sits six orders under what this model is
    // worth. It also keeps the double kernels' Payne-Hanek argument reduction
    // out of the image, which exists for arguments around 1e300.
    //
    // The JULIAN DATES stay double, and must: at 2.46e6 a float ulp is six
    // hours, and the hour angle is the difference of two of them.
    static double _sin(float r)  { return (double)sinf(r); }
    static double _cos(float r)  { return (double)cosf(r); }
    static double _asin(float x) { return (double)asinf(x); }
    static double _acos(float x) { return (double)acosf(x); }

    // Degrees into [0, 360). The arguments here are a few thousand degrees, so
    // the truncating cast is exact and fmod() is not needed for it.
    static double _wrap360(double deg) {
        deg -= 360.0 * (double)(long)(deg / 360.0);
        return (deg < 0.0) ? deg + 360.0 : deg;
    }

    // Sun's ecliptic longitude (degrees) for a count of days @p t since J2000,
    // also returning the solar mean anomaly @p M_deg (its sines feed the
    // transit correction). Both are normalised to [0, 360).
    static double _ecliptic_longitude(double t, double& M_deg) {
        double M  = _wrap360(357.5291 + 0.98560028 * t);
        float  Mr = (float)(M * DEG2RAD);

        // equation of the centre (first three harmonics)
        double C = 1.9148 * _sin(Mr) + 0.0200 * _sin(2.0f * Mr)
                 + 0.0003 * _sin(3.0f * Mr);

        // + 282.9372 = 180 (sun vs earth) + 102.9372 (perihelion argument)
        M_deg = M;
        return _wrap360(M + C + 282.9372);
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
        return 24.0 * _acos((float)cos_H0) / SOLAR_PI;
    }

    // Solar position

    SolarPosition solar_position(uint64_t now_ms, double lat_deg, double lon_deg) {
        double jd_now = (double)unix_ms_from_uptime_ms(now_ms) / MS_PER_DAY + JD_UNIX;

        // Integer day count since J2000 whose solar noon is nearest "now" (so
        // the hour angle below lands near [-180, 180] deg). lon east +, so an
        // eastern location's noon falls earlier in UTC.
        // Round half away from zero, which is llround()'s rule; the value is a
        // day count, so it is nowhere near overflowing a long.
        const double days = jd_now - JD_J2000 + lon_deg / 360.0;
        long n = (long)(days >= 0.0 ? days + 0.5 : days - 0.5);

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
                        + 0.0053 * _sin((float)Mr) - 0.0069 * _sin(2.0f * (float)lambda_r);

        // Declination from the ecliptic longitude.
        double sin_decl = _sin((float)lambda_r) * _sin((float)(OBLIQUITY_DEG * DEG2RAD));
        double cos_decl = (double)sqrtf((float)(1.0 - sin_decl * sin_decl));

        // Hour angle: Earth turns 360 deg per solar day, zero at transit. cos is
        // periodic, so there is no need to wrap it into range.
        double H_r = (jd_now - jtransit) * 360.0 * DEG2RAD;

        double lat_r   = lat_deg * DEG2RAD;
        double sin_lat = _sin((float)lat_r);
        double cos_lat = _cos((float)lat_r);

        SolarPosition p;
        // The transit as a wall-clock instant, which is what a caller wanting
        // sunrise and sunset needs; the elevation math below uses jtransit
        // directly and never converts.
        p.transit_ms = (uint64_t)((jtransit - JD_UNIX) * MS_PER_DAY);
        // Current altitude (elevation) above the horizon.
        double sin_alt = sin_lat * sin_decl + cos_lat * cos_decl * _cos((float)H_r);
        p.elevation = _asin((float)sin_alt);
        // Peak altitude at solar noon, where the hour angle is 0 (cos(H) = 1);
        // reduces to the classic 90 - |lat - decl|. Reported as a magnitude, so
        // deep polar night still gives the elevation ratio a positive reference.
        double sin_alt_peak = sin_lat * sin_decl + cos_lat * cos_decl;
        double alt_peak     = _asin((float)sin_alt_peak);
        p.peak_elevation    = (alt_peak < 0.0) ? -alt_peak : alt_peak;
        // Before transit the sun is still climbing toward noon; after, falling.
        p.rising = jd_now < jtransit;

        // Today's daylight span from the current declination.
        p.day_length_h = _day_length_hours(sin_lat, cos_lat, sin_decl, cos_decl);
        // Longest day at this latitude: the summer solstice, where the
        // declination reaches the obliquity toward this hemisphere (same sign as
        // the latitude). At the equator this is a flat 12 h either way.
        double sign_lat     = (sin_lat >= 0.0) ? 1.0 : -1.0;
        double sin_decl_max = _sin((float)(OBLIQUITY_DEG * DEG2RAD)) * sign_lat;
        double cos_decl_max = _cos((float)(OBLIQUITY_DEG * DEG2RAD));
        p.max_day_length_h  = _day_length_hours(sin_lat, cos_lat, sin_decl_max, cos_decl_max);
        return p;
    }

    // SunPositionTracker

    // Wall-clock sanity floor (ms since the UNIX epoch, ~2020-09): below this
    // the RTC is unset (no NTP yet) and the date-dependent solar math is
    // meaningless, so we wait.
    static const uint64_t WALL_CLOCK_VALID_MS = 1600000000000ull;

    SunPositionTracker::SunPositionTracker(const SetupContext& ctx)
        : Component(ctx, "SunPositionTracker"),
          _pos(), _next_ms(0), _computed(false) {}

    void SunPositionTracker::set_config(const GeoConfig& geo) {
        _geo = geo;
        _located = true;
        _computed = false;
    }


    void SunPositionTracker::register_tasks() {
        _tasks->add(*this, "sys_sun",
                    Config::SUN_PERIOD_MS, Tasks::PHASE_SERVICE);
    }

    void SunPositionTracker::wire(Vars::Bank& io) {
        _io          = &io;
        _r_ready     = io.find("sol_ready");
        _r_from_rise = io.find("sol_fromRise_min");
        _r_from_set  = io.find("sol_fromSet_min");
        _r_elev      = io.find("sol_elev_deg");
    }

    // Clamped to a day either side: the offsets are read against boundaries a
    // few hours out, and a wall clock that jumped a year must not arrive as a
    // plausible-looking number of minutes.
    static int16_t _minutes(int64_t v) {
        if (v < -1440) return -1440;
        if (v >  1440) return  1440;
        return (int16_t)v;
    }

    void SunPositionTracker::_publish() {
        if (_io == nullptr) return;
        // ONE row for the lot: a law downstream cannot ask the bank whether a
        // solar output is believable, and they go together - the tracker has a
        // location and a clock, or it has neither.
        if (_r_ready.index != Vars::VAR_NONE) _io->var(_r_ready).set(_computed);
        // Nothing computed yet: every boot passes through this for as long as
        // NTP takes, and the offsets ARE the sun, so they hold rather than
        // publishing a day nobody has placed.
        if (!_computed) return;

        // Signed minutes either side of the two events, the form every
        // boundary in a phase table is declared in. Both belong to the same
        // solar day, so one is always past and the other future.
        const int64_t now = (int64_t)HAL::Clock::wall_ms();
        if (_r_from_rise.index != Vars::VAR_NONE)
            _io->var(_r_from_rise).set_i(_minutes((now - (int64_t)sunrise_ms()) / 60000));
        if (_r_from_set.index != Vars::VAR_NONE)
            _io->var(_r_from_set).set_i(_minutes((now - (int64_t)sunset_ms()) / 60000));
        // Degrees, because that is what a person reads. The math is in radians.
        if (_r_elev.index != Vars::VAR_NONE)
            _io->var(_r_elev).set((float)(_pos.elevation * 57.2957795));
    }

    void SunPositionTracker::on_service(uint64_t now_ms) {
        // The elevation drifts slowly, so recompute only on cadence (or on the
        // first valid beat / after a geo change, both signalled by !_computed).
        // A real (NTP) wall clock first: the solar math is date-dependent.
        if (_located && HAL::Clock::wall_ms() >= WALL_CLOCK_VALID_MS &&
            (!_computed || now_ms >= _next_ms)) {
            _pos      = solar_position(now_ms, (double)_geo.lat_deg,
                                       (double)_geo.lon_deg);
            _computed = true;
            _next_ms  = now_ms + Config::SUN_RECOMPUTE_MS;
        }
        // ...but publish every tick regardless: the minute offsets are taken
        // from the wall clock, which moves while the trig does not.
        _publish();
    }

}}

#endif // BOTANIK_FEATURE_SUN_TRACKER
