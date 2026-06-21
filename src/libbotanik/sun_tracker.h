#ifndef LIBBOTANIK_SUN_TRACKER_H
#define LIBBOTANIK_SUN_TRACKER_H

/**
 * @file sun_tracker.h
 * @brief SunPositionTracker - tracks the next sunrise / sunset and the day-or-night
 *        state for the site location from the PLC `geo` config block.
 *
 * Config-driven (ConfigHandler): restarts when the operator changes the `geo` block
 * (latitude / longitude). The solar math lives in utils/sun.h; this component drives it
 * on a schedule - it (re)computes the next sunrise and sunset whenever one passes, and
 * only once the wall clock is valid (the math needs a real date, so it waits for an NTP
 * sync). Other code reads is_daytime() / the event times to drive light-dependent logic.
 */

#include "libbotanik/config_handler.h"
#include "libbotanik/utils/sun.h"

namespace Botanik {

    /** @brief Tracks sunrise / sunset + day/night for the geo-configured location. */
    class SunPositionTracker : public ConfigHandler {
        /** @name State
         *  @{ */
        Utils::SunTimes _times;     ///< next sunrise + sunset (uptime-domain ms)
        bool            _is_day;    ///< sun currently above the horizon
        bool            _computed;  ///< _times holds a valid computation
        /** @} */

    public:
        SunPositionTracker(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger = nullptr);

        /** @brief Sun currently above the horizon (false until first computed). */
        bool is_daytime() const { return _is_day; }
        /** @brief Next sunrise / sunset as uptime-domain ms (per-event valid flags). */
        const Utils::SunTimes& times() const { return _times; }
        /** @brief True once a valid computation exists (needs a set wall clock). */
        bool ready() const { return _computed; }

    protected:
        uint32_t config_serial() const override;
        void on_config_changed(uint64_t now_ms) override;
        void on_step(uint64_t now_ms) override;

    private:
        /** @brief Recompute the next sun times from the geo config and update day/night. */
        void _recompute(uint64_t now_ms);
    };

}

#endif // LIBBOTANIK_SUN_TRACKER_H
