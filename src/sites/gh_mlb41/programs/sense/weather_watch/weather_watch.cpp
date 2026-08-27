/**
 * @file weather_watch.cpp
 * @brief HAND-WRITTEN - the body of the `weather_watch` POU.
 */

#include "weather_watch.h"

namespace Site {

void WeatherWatch::run(ProgramContext& c) { (void)c; }

void WeatherWatch::observe(ProgramContext& c) {
    const bool measured = wind.valid() &&
                          rain.valid();
    if (measured) _seen = true;
    // A station that has never answered is the bus still coming up; one that
    // has and has stopped is the fault the windows are already shut for.
    station.set(_seen && !measured, true);
    if (!measured) degraded();

    // A STORM is sustained wind: cli_windShutEnd_mps, 20.8, which is 75 kmph and
    // Beaufort 9. It is the same number that shuts the roof, because it is the
    // same fact. Reading the windows' verdict instead put a storm on the ribbon
    // for a stiff breeze or a passing shower.
    //
    // The MEAN, not the gust: a gust is a moment and a storm is a condition.
    // The row's own dwell is what makes it sustained. Only once the station has
    // answered, or every boot announces one.
    storm.set(_seen && wind.valid() && wind.get() >= storm_wind.get(), true);

    // A published copy of the irradiance, which nothing controls on: the field
    // row is SCOPE_MI and so carries no Modbus TCP register, and a historian on
    // the plant network has no other way to poll it. Its own network, of one
    // input, so it carries that input's quality and no other's.
    solar.set(irrad.get(), irrad.valid());
    if (!irrad.valid()) degraded();

    // How long the house has been dry. A gauge that is not answering says
    // nothing about the weather, so the clock stands rather than running on.
    //
    // It runs from BOOT, not from the first shower this unit happened to
    // witness. Before then the answer is "at least this long", which is both
    // true and what the roof cares about; waiting for a first raindrop left the
    // home screen blank for however many dry weeks it took.
    if (rain.valid()) {
        if (rain.get() > 0.0f) _dry_ms = 0;
        else                   _dry_ms += c.dt_ms;
    }
    const uint64_t mins = _dry_ms / 60000ull;
    since_rain.set((uint16_t)(mins > 65535ull ? 65535ull : mins), rain.valid());
}

}  // namespace Site
