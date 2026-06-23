#ifndef UTILS_SUN_H
#define UTILS_SUN_H

/**
 * @file sun.h
 * @brief Solar position utilities - the sun's declination and the next
 *        sunrise/sunset for a given location, as absolute timestamps.
 *
 * The math is the standard NOAA / "sunrise equation" approximation (good to
 * ~1 minute worldwide): accurate declination, the equation of time folded into
 * the solar-transit term, and the conventional -0.833 deg sunrise/sunset
 * altitude (atmospheric refraction plus the sun's apparent radius). All angles
 * are handled internally in radians; declination is returned in radians.
 *
 * Timestamps are uptime-domain ms, the same domain as HAL::uptime_ms() and the
 * inputs to utils/time.h, so a returned event can be compared straight against
 * "now". Location is plain degrees (north +, east +); no timezone is needed,
 * since an absolute instant is timezone-independent and longitude already fixes
 * solar noon in UTC.
 */

#include <cstdint>

namespace Botanik { namespace Utils {

    /** @name Types
     *  @{ */

    /** @brief A solar event as an absolute uptime-domain ms timestamp
     *  (comparable to HAL::uptime_ms()). @p valid is false at polar day/night
     *  (no event); @p time_ms is then unspecified. */
    struct SunEvent {
        uint64_t time_ms;
        bool     valid;
    };

    /** @brief The next sunrise and the next sunset after a reference instant.
     *  The two are resolved independently, so near dusk @ref sunrise is
     *  tomorrow's while @ref sunset is still today's. */
    struct SunTimes {
        SunEvent sunrise;
        SunEvent sunset;
    };

    /** @} */

    /** @name Solar position
     *  @{ */

    /** @brief Sun's declination angle (radians) at the given instant. Ranges
     *  over +/-23.44 deg across the year (positive towards the northern
     *  summer). */
    double sun_declination(uint64_t now_ms);

    /** @brief Next sunrise and sunset strictly after @p now_ms at the given
     *  location (@p lat_deg north +, @p lon_deg east +). Each is the first
     *  future occurrence, searched forward far enough that polar latitudes
     *  still resolve; an event with none inside that horizon comes back with
     *  valid = false. */
    SunTimes next_sun_times(uint64_t now_ms, double lat_deg, double lon_deg);

    /** @} */

}}

#endif // UTILS_SUN_H
