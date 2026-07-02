#ifndef SUN_SUN_TRACKER_H
#define SUN_SUN_TRACKER_H

/**
 * @file sun/sun_tracker.h
 * @brief Solar elevation math and SunPositionTracker.
 *
 * Two layers, both in the Botanik::Sun namespace:
 *
 *  - The solar-position math (solar_elevation()): the sun's elevation angle
 *    above the horizon for a given location and instant. It is the standard
 *    NOAA / "sunrise equation" formulation (good to ~1 minute worldwide):
 *    ecliptic longitude and declination from the day count, solar noon with the
 *    equation of time folded in, then the altitude from latitude, declination
 *    and hour angle. All angles are handled internally in radians; the result is
 *    returned in radians (negative below the horizon). Time is uptime-domain ms,
 *    the same domain as HAL::Clock::uptime_ms() and utils/time.h. Location is
 *    plain degrees (north +, east +); no timezone is needed, since an absolute
 *    instant is timezone-independent and longitude already fixes solar noon in
 *    UTC.
 *
 *  - SunPositionTracker: tracks the current solar elevation for the site
 *    location from the PLC `geo` config block. Config-driven (ConfigHandler):
 *    restarts on a `geo` change, and computes only once the wall clock is valid
 *    (the math needs a real date, so it waits for an NTP sync).
 *
 * Gated by BOTANIK_FEATURE_SUN.
 */

#include <cstdint>

// libbotanik_config.h pulls the site config then the feature flags in that
// order (so a site override wins) - include it, not features.h directly, so
// this header is safe to include standalone (e.g. from a test).
#include "libbotanik_config.h"

#if BOTANIK_FEATURE_SUN

#include "libbotanik/core/config.h"

namespace Botanik { namespace Sun {

    /** @name Solar position
     *  @{ */

    /** @brief Sun's elevation angle (radians) above the horizon at @p now_ms,
     *  seen from the given location (@p lat_deg north +, @p lon_deg east +).
     *  Positive above the horizon, negative below; peaks each day at solar noon
     *  at 90 deg - |lat - declination|. */
    double solar_elevation(uint64_t now_ms, double lat_deg, double lon_deg);

    /** @} */

    /** @brief Tracks the sun's elevation angle for the geo-configured
     *  location. */
    class SunPositionTracker : public ConfigHandler {
        /** @name State
         *  @{ */
        double   _elevation;   ///< sun's elevation above the horizon (radians)
        uint64_t _next_ms;     ///< uptime ms of the next scheduled recompute
        bool     _computed;    ///< _elevation holds a valid computation
        /** @} */

    public:
        SunPositionTracker(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger = nullptr);

        /** @brief Sun's elevation above the horizon (radians; negative below).
         *  Zero until the first computation (see ready()). */
        double elevation() const { return _elevation; }
        /** @brief True once a valid computation exists (needs a set wall
         *  clock). */
        bool ready() const { return _computed; }

    protected:
        uint32_t config_serial() const override;
        void on_config_changed(uint64_t now_ms) override;
        void on_step(uint64_t now_ms) override;
    };

}}

#endif // BOTANIK_FEATURE_SUN
#endif // SUN_SUN_TRACKER_H
