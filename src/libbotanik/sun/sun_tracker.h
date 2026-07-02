#ifndef SUN_SUN_TRACKER_H
#define SUN_SUN_TRACKER_H

/**
 * @file sun/sun_tracker.h
 * @brief SunPositionTracker - tracks the next sunrise / sunset and day/night
 *        state for the site location from the PLC `geo` config block.
 *
 * Config-driven (ConfigHandler): restarts on a `geo` change. The solar math is
 * in sun/sun.h; this recomputes whenever an event passes, and only once the
 * wall clock is valid (the math needs a real date, so it waits for an NTP
 * sync).
 *
 * Gated by BOTANIK_FEATURE_SUN.
 */

// libbotanik_config.h pulls the site config then the feature flags in that
// order (so a site override wins) - include it, not features.h directly, so
// this header is safe to include standalone (e.g. from a test).
#include "libbotanik_config.h"

#if BOTANIK_FEATURE_SUN

#include "libbotanik/core/config.h"
#include "libbotanik/sun/sun.h"

namespace Botanik { namespace Sun {

    /** @brief Tracks sunrise / sunset + day/night for the geo-configured
     *  location. */
    class SunPositionTracker : public ConfigHandler {
        /** @name State
         *  @{ */
        SunTimes _times;     ///< next sunrise + sunset (uptime ms)
        bool     _is_day;    ///< sun currently above the horizon
        bool     _computed;  ///< _times holds a valid computation
        /** @} */

    public:
        SunPositionTracker(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger = nullptr);

        /** @brief Sun currently above the horizon (false until first computed).
         */
        bool is_daytime() const { return _is_day; }
        /** @brief Next sunrise / sunset as uptime-domain ms (per-event valid
         *  flags). */
        const SunTimes& times() const { return _times; }
        /** @brief True once a valid computation exists (needs a set wall
         *  clock). */
        bool ready() const { return _computed; }

    protected:
        uint32_t config_serial() const override;
        void on_config_changed(uint64_t now_ms) override;
        void on_step(uint64_t now_ms) override;

    private:
        /** @brief Recompute the next sun times from the geo config and update
         *  day/night. */
        void _recompute(uint64_t now_ms);
    };

}}

#endif // BOTANIK_FEATURE_SUN
#endif // SUN_SUN_TRACKER_H
