/**
 * @file sun_tracker.cpp
 * @brief SunPositionTracker implementation.
 */

#include "libbotanik/sun_tracker.h"
#include "libbotanik/hal/platform.h"   // HAL::wall_clock_ms

namespace Botanik {

    // Wall-clock sanity floor (ms since the UNIX epoch, ~2020-09): below this the
    // RTC is unset (no NTP yet) and the date-dependent solar math is meaningless,
    // so we wait.
    static const uint64_t WALL_CLOCK_VALID_MS = 1600000000000ull;

    SunPositionTracker::SunPositionTracker(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger)
        : ConfigHandler(ctx, logger, "SunPositionTracker"), _is_day(false), _computed(false) {
        _times.sunrise.valid = false;
        _times.sunset.valid  = false;
    }

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
        if (c == nullptr || c->geo.serial == 0) return;  // no location configured
        // wait for a real (NTP) time before the date-dependent solar math
        if (HAL::wall_clock_ms() < WALL_CLOCK_VALID_MS) return;

        if (!_computed) { _recompute(now_ms); return; }

        // next_sun_times() always returns future events, so once we pass either
        // one it is time to roll forward to the following day's.
        bool passed = (_times.sunrise.valid && now_ms >= _times.sunrise.time_ms) ||
                      (_times.sunset.valid  && now_ms >= _times.sunset.time_ms);
        if (passed) _recompute(now_ms);
    }

    void SunPositionTracker::_recompute(uint64_t now_ms) {
        const PlcIO::PlcOutputConfig_t* c = config();
        double lat = (double)c->geo.latitude_e7  / 1e7;
        double lon = (double)c->geo.longitude_e7 / 1e7;

        bool had = _computed;
        _times = Utils::next_sun_times(now_ms, lat, lon);
        _computed = true;

        // Daytime when the sun sets before it next rises. Both events must be valid;
        // at polar latitudes (one invalid) keep the previous state.
        bool day = _is_day;
        if (_times.sunrise.valid && _times.sunset.valid)
            day = _times.sunset.time_ms < _times.sunrise.time_ms;

        // Log only real transitions, not the initial detection.
        if (had && day != _is_day && _logger != nullptr)
            _logger->notice(PlcIO::TIME, day ? "sunrise" : "sunset");
        _is_day = day;
    }

}
