// REVIEW_35: DONE
#ifndef SUN_SUN_TRACKER_H
#define SUN_SUN_TRACKER_H

/**
 * @file sun/sun_tracker.h
 * @brief Solar elevation math and SunPositionTracker.
 *
 */

#include <cstdint>                      // uint32_t/uint64_t

#include "libbotanik/config/base.h"       // the BOTANIK_FEATURE_* gates

#include "libbotanik/core/component.h"  // Component
#include "libbotanik/vars/params.h"     // Vars::Bank / get_f32

#if BOTANIK_FEATURE_SUN_TRACKER

namespace Botanik { namespace Sun {

    /** @name Solar elevation angle computation
     *  @{ */

    /** @brief A solar-position sample for one instant and location: the sun's
     *  current elevation, the day's peak elevation (reached at solar noon),
     *  whether it is still climbing toward that peak, and the daylight duration
     *  (today's and the yearly maximum at this latitude). */
    struct SolarPosition {
        double elevation;       ///< Current elevation angle (radians; negative below horizon).
        double peak_elevation;  ///< The day's peak elevation magnitude at solar noon (radians; always >= 0).
        bool   rising;          ///< True before solar noon (climbing), false after (descending).
        double day_length_h;    ///< Today's daylight duration (hours); 0 polar night, 24 polar day.
        double max_day_length_h;///< The longest daylight at this latitude (hours), at the summer solstice.

        /** @brief Elevation as a signed fraction of the day's peak: ~1.0 at
         *  solar noon, negative below the horizon (peak is always >= 0, so the
         *  sign is elevation()'s). Zero only when the peak grazes the horizon. */
        double ratio() const {
            return peak_elevation > 0.0 ? elevation / peak_elevation : 0.0;
        }
    };

    /** @brief Full solar position (see SolarPosition) at @p now_ms, seen from the
     *         given location (@p lat_deg north +, @p lon_deg east +). */
    SolarPosition solar_position(uint64_t now_ms, double lat_deg, double lon_deg);

    /** @}
     *  @name Stateful sun position tracker
     *  @{ */

    /** @brief Tracks the sun's elevation for the geo-configured location:
     *         current angle, the day's peak, the angle-to-peak ratio, and
     *         whether the sun is rising or falling. */
    class SunPositionTracker : public Component {
        const Vars::Bank* _cfg;   ///< config source (may be null in tests). Read by name.
        SolarPosition _pos;    ///< Cached solar position (see ready()).
        uint64_t _next_ms;     ///< Timestamp the next scheduled recompute.
        bool     _computed;    ///< Position successfully acquired.

    public:
        SunPositionTracker(const SetupContext& ctx, const Vars::Bank* cfg);

        /** @brief Sun's elevation angle above the horizon (radians; negative below).
         *  @note Zero until the first computation (see ready()). */
        double elevation() const { return _pos.elevation; }

        /** @brief The day's peak elevation angle, reached at solar noon (radians).
         *  @note Zero until the first computation (see ready()). */
        double peak_elevation() const { return _pos.peak_elevation; }

        /** @brief Current elevation as a fraction of the day's peak (1.0 at solar
         *  noon, <= 0 below the horizon). @see SolarPosition::ratio(). */
        double elevation_ratio() const { return _pos.ratio(); }

        /** @brief True while the sun is climbing toward solar noon, false while
         *  it descends toward sunset. */
        bool rising() const { return _pos.rising; }

        /** @brief Today's daylight duration in hours (0 in polar night, 24 in
         *  polar day). @note Zero until the first computation (see ready()). */
        double day_length_h() const { return _pos.day_length_h; }

        /** @brief The longest daylight duration at this latitude (hours), reached
         *  at the summer solstice. @note Zero until the first computation. */
        double max_day_length_h() const { return _pos.max_day_length_h; }

        /** @brief True once a valid computation exists (needs a set wall clock). */
        bool ready() const { return _computed; }

    protected:
        /** @brief Get the serial number of the geographical configuration.
         * @implements Component
          */
        uint32_t param_serial() const override;

        /** @brief Invalidate the current position on a location change. */
        void on_param_changed(uint64_t now_ms) override;

        /** @brief Recompute the solar position after a delay has elapsed. */
        void on_service(uint64_t now_ms) override;
    };

    /** @} */

}}

#endif // BOTANIK_FEATURE_SUN_TRACKER

#endif // SUN_SUN_TRACKER_H
