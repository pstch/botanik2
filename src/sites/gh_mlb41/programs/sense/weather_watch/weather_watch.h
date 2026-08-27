/**
 * @file weather_watch.h
 * @brief The declaration half of the `weather_watch` POU: its rows, its
 *        blocks, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_WEATHER_WATCH_H
#define SITE_PROGRAM_WEATHER_WATCH_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

namespace Site {

using namespace Botanik;

/**
 * @brief The weather station, watched: whether it is answering, and whether what
 *        it says is a storm.
 */
class WeatherWatch final : public Plc::Program {

public:

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<float> wind;
    Vars::RefIn<float> rain;
    Vars::RefIn<float> storm_wind;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<bool>     storm;
    Vars::RefOut<uint16_t> since_rain;

    /** @} */

private:

    /** @name VAR - what this law remembers
     *  @{ */

    /** @brief Whether the station has EVER answered. Before it has, silence
     *  is bring-up; after, it is a fault. @see AcSupply */
    bool     _seen = false;
    /** @brief Ms since the gauge last read wet, and whether it ever has in
     *  this run. A boot does not know when it last rained, so before the
     *  first shower the row is held rather than reading "just now". */
    uint64_t _dry_ms = 0;

    /** @} */

public:
    WeatherWatch(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "weather_watch", Tasks::GROUP_SLOW, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
    void observe(ProgramContext& c) override;  ///< reports in EVERY state
};

inline void WeatherWatch::bind(Vars::Bank& io) {
    wind       = _bind_input<float>(io, "cli_wind_mps");
    rain       = _bind_input<float>(io, "cli_rain_mmph");
    storm_wind = _bind_input<float>(io, "cli_windShutEnd_mps");
    storm      = _bind_output<bool>(io, "alarm_weather_storm");
    since_rain = _bind_output<uint16_t>(io, "cli_sinceRain_min");

    set_health_row(_bind_output<uint8_t>(io, "prog_weather_watch"));
}

}  // namespace Site

#endif // SITE_PROGRAM_WEATHER_WATCH_H
