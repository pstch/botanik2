/**
 * @file weather_watch.cpp
 * @brief HAND-WRITTEN - the body of the `weather_watch` POU.
 */

#include "weather_watch.gen.h"

namespace Site {

void WeatherWatch::run(ProgramContext& c) { (void)c; }

void WeatherWatch::observe(ProgramContext& c) {
    const bool measured = wind.valid() &&
                          rain.valid();
    if (measured) _seen = true;
    // A station that has never answered is the bus still coming up; one that
    // has and has stopped is the fault the windows are already shut for.
    station.set(_seen && !measured);
    if (!measured) degraded();

    // The windows' own verdict, not a second pair of thresholds beside it -
    // and only once the station has answered at least once. Before that the
    // windows are shut on the BLIND interlock, which is caution rather than
    // weather, and annunciating it puts a storm on the ribbon at every boot.
    storm.set(_seen && (rain_lock.get() || wind_lock.get()));

    // How long the house has been dry. A gauge that is not answering says
    // nothing about the weather, so the clock stands rather than running on.
    if (rain.valid()) {
        if (rain.get() > 0.0f) { _dry_ms = 0; _wet_seen = true; }
        else if (_wet_seen)       { _dry_ms += c.dt_ms; }
    }
    if (_wet_seen) {
        const uint64_t mins = _dry_ms / 60000ull;
        since_rain.set((uint16_t)(mins > 65535ull ? 65535ull : mins));
    } else {
        since_rain.hold();
    }
}

}  // namespace Site
