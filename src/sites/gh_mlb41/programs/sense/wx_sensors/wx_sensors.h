/**
 * @file wx_sensors.h
 * @brief The declaration half of the `wx_sensors` POU: its rows, its block,
 *        and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_WX_SENSORS_H
#define SITE_PROGRAM_WX_SENSORS_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"
#include "../../../blocks/weather_probes/weather_probes.h"

namespace Site {

using namespace Botanik;

/**
 * @brief The instruments, split from the derivations they feed: two weather
 *        stations voted down to one reading per quantity.
 */
class WxSensors final : public Plc::Program {

public:

    /** @name VAR - the two stations' rows
     *  The block's declared interface, filled by bind(). @{ */

    WeatherProbeRefs stations_rows;

    /** @} */

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<uint16_t> tau_ms;

    /** @} */

private:

    /** @name VAR - function blocks
     *  @{ */

    WeatherProbes stations;

    /** @} */

public:
    WxSensors(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "wx_sensors", Tasks::GROUP_FAST, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;

};

inline void WxSensors::bind(Vars::Bank& io) {
    stations_rows.wind[0]  = _bind_input<float>(io, "cli_wind1_mps");
    stations_rows.wind[1]  = _bind_input<float>(io, "cli_wind2_mps");
    stations_rows.gust[0]  = _bind_input<float>(io, "cli_windGust1_mps");
    stations_rows.gust[1]  = _bind_input<float>(io, "cli_windGust2_mps");
    stations_rows.rain[0]  = _bind_input<float>(io, "cli_rain1_mmph");
    stations_rows.rain[1]  = _bind_input<float>(io, "cli_rain2_mmph");
    stations_rows.irrad[0] = _bind_input<float>(io, "cli_irrad1_Wpm2");
    stations_rows.irrad[1] = _bind_input<float>(io, "cli_irrad2_Wpm2");
    stations_rows.wind_out  = _bind_output<float>(io, "cli_wind_mps");
    stations_rows.gust_out  = _bind_output<float>(io, "cli_windGust_mps");
    stations_rows.rain_out  = _bind_output<float>(io, "cli_rain_mmph");
    stations_rows.irrad_out = _bind_output<float>(io, "cli_irrad_Wpm2");
    stations_rows.fault     = _bind_output<bool>(io, "alarm_wx_station");

    tau_ms = _bind_input<uint16_t>(io, "cli_wxTau_ms");
    stations.bind(stations_rows, 0.001f * (float)tau_ms.get());
}

}  // namespace Site

#endif // SITE_PROGRAM_WX_SENSORS_H
