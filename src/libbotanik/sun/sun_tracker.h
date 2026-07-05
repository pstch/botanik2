// REVIEW_35: DONE
#ifndef SUN_SUN_TRACKER_H
#define SUN_SUN_TRACKER_H

/**
 * @file sun/sun_tracker.h
 * @brief Solar elevation math and SunPositionTracker.
 *
 */

#include <cstdint>

#include "libbotanik_config.h"
#include "libbotanik/core/component.h"
#include "libbotanik/plcio/sync.h"      // PlcSyncContext + plc_config()

#if BOTANIK_FEATURE_SUN

namespace Botanik { namespace Sun {

    /** @name Solar elevation angle computation
     *  @{ */

    /** @brief Sun's elevation angle (radians) above the horizon at @p now_ms,
     *         seen from the given location (@p lat_deg north +, @p lon_deg east +).
     *  @note Positive above the horizon, negative below; peaks each day at solar noon. */
    double solar_elevation(uint64_t now_ms, double lat_deg, double lon_deg);

    /** @} 
     *  @name Stateful sun position tracker
     *  @{ */

    /** @brief Tracks the sun's elevation angle for the geo-configured location. */
    class SunPositionTracker : public Component {
        PlcIO::PlcSyncContext* _ctx;   ///< PLC config source (may be null in tests).
        double   _elevation;   ///< Sun's elevation le cached value (radians).
        uint64_t _next_ms;     ///< Timestamp the next scheduled recompute.
        bool     _computed;    ///< Elevation angle successfully acquired.

        /** @brief The generic config block (or null with no ctx). */
        const PlcIO::PlcOutputConfig_t* config() const { return PlcIO::plc_config(_ctx); }

    public:
        SunPositionTracker(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger = nullptr);

        /** @brief Sun's elevation above the horizon (radians; negative below).
         *  @note Zero until the first computation (see ready()). */
        double elevation() const { return _elevation; }

        /** @brief True once a valid computation exists (needs a set wall clock). */
        bool ready() const { return _computed; }

    protected:
        /** @brief Get the serial number of the geographical configuration.
         * @implements Component
          */
        uint32_t config_serial() const override;

        /** @brief Invalidate the current . */
        void on_config_changed(uint64_t now_ms) override;

        /** @brief Recompute the solar elevation after a delay has elapsed. */
        void on_step(uint64_t now_ms) override;
    };

    /** @} */

}}

#endif // BOTANIK_FEATURE_SUN

#endif // SUN_SUN_TRACKER_H
