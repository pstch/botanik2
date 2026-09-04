#ifndef SUN_SUN_TRACKER_H
#define SUN_SUN_TRACKER_H

/**
 * @file sun/sun_tracker.h
 * @brief Solar elevation math and SunPositionTracker.
 *
 */

#include <cstdint>

#include "libbotanik/config/base.h"

#include "libbotanik/core/component.h"
#include "libbotanik/tasks/tasks.h"
#include "libbotanik/vars/model.h"
#include "libbotanik/vars/rows.h"

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
        uint64_t transit_ms;    ///< Solar noon nearest this sample, UNIX ms.

        /** @brief Sunrise and sunset around that noon, UNIX ms. Symmetric about
         *  the transit by construction, so a polar day gives noon +/- 12 h and a
         *  polar night gives noon twice - equal timestamps mean neither
         *  happens today. */
        uint64_t sunrise_ms() const { return transit_ms - _half_day_ms(); }
        uint64_t sunset_ms()  const { return transit_ms + _half_day_ms(); }

        /** @brief Elevation as a signed fraction of the day's peak: ~1.0 at
         *  solar noon, negative below the horizon (peak is always >= 0, so the
         *  sign is elevation()'s). Zero only when the peak grazes the horizon. */
        double ratio() const {
            return peak_elevation > 0.0 ? elevation / peak_elevation : 0.0;
        }

    private:
        uint64_t _half_day_ms() const {
            return (uint64_t)(day_length_h * 1800000.0);   // h/2, in ms
        }
    };

    /** @brief Full solar position (see SolarPosition) at @p now_ms, seen from the
     *         given location (@p lat_deg north +, @p lon_deg east +). */
    SolarPosition solar_position(uint64_t now_ms, double lat_deg, double lon_deg);

    /** @}
     *  @name Stateful sun position tracker
     *  @{ */

    /** @brief Where on earth this site is. Degrees, positive north and east.
     *  Both together or neither: a latitude belonging to one place and a
     *  longitude to another is a position nowhere on earth. */
    struct GeoConfig {
        float lat_deg = 0.0f;
        float lon_deg = 0.0f;
    };

    /** @brief Tracks the sun's elevation for the geo-configured location:
     *         current angle, the day's peak, the angle-to-peak ratio, and
     *         whether the sun is rising or falling. */
    class SunPositionTracker : public Component {
        SolarPosition _pos;    ///< Cached solar position (see ready()).
        uint64_t _next_ms;     ///< Timestamp the next scheduled recompute.
        bool     _computed;    ///< Position successfully acquired.
        /** @brief The location in force, and whether anyone has given one. An
         *  unlocated tracker computes nothing: 0,0 is a real place. */
        GeoConfig _geo;
        bool      _located = false;

    public:
        explicit SunPositionTracker(const SetupContext& ctx);

        /** @brief Adopt @p geo and drop the old computation; on_service()
         *  recomputes once the wall clock is valid. */
        void set_config(const GeoConfig& geo);

        /** @brief Sun's elevation angle above the horizon (radians; negative below).
         *  @note Zero until the first computation (see ready()). */
        double elevation() const { return _pos.elevation; }

        /** @brief The day's peak elevation angle, reached at solar noon (radians).
         *  @note Zero until the first computation (see ready()). */
        double peak_elevation() const { return _pos.peak_elevation; }


        /** @brief True while the sun is climbing toward solar noon, false while
         *  it descends toward sunset. */
        bool rising() const { return _pos.rising; }

        /** @brief Today's daylight duration in hours (0 in polar night, 24 in
         *  polar day). @note Zero until the first computation (see ready()). */
        double day_length_h() const { return _pos.day_length_h; }

        /** @brief The longest daylight duration at this latitude (hours), reached
         *  at the summer solstice. @note Zero until the first computation. */
        double max_day_length_h() const { return _pos.max_day_length_h; }

        /** @brief Today's sunrise and sunset, UNIX ms. @see SolarPosition.
         *  @note Zero until the first computation (see ready()). */
        uint64_t sunrise_ms() const { return _pos.sunrise_ms(); }
        uint64_t sunset_ms()  const { return _pos.sunset_ms(); }

        /** @brief True once a valid computation exists (needs a set wall clock). */
        bool ready() const { return _computed; }

        /** @brief One entry, PHASE_SERVICE: the position is a slow observation,
         *  and nothing in the beat waits on it. */
        void register_tasks() override;

        /** @brief Bind the four rows this publishes. Unwired, the tracker
         *  still computes and simply says nothing. */
        void wire(Vars::Bank& io);

    protected:
        /** @brief Recompute the solar position after a delay has elapsed. */
        void on_service(uint64_t now_ms) override;

    private:
        /** @brief Put the position on its rows. Every service tick, not every
         *  recompute: the minute offsets move with the WALL CLOCK while the
         *  trig behind them is an hour's worth of unchanged. */
        void _publish();

        Vars::Bank*  _io = nullptr;
    };

    /** @} */

}}

#endif // BOTANIK_FEATURE_SUN_TRACKER

#endif // SUN_SUN_TRACKER_H
