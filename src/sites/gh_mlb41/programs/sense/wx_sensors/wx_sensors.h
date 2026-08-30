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

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<float> wind[2];
    Vars::RefIn<float> gust[2];
    Vars::RefIn<float> rain[2];
    Vars::RefIn<float> irrad[2];
    Vars::RefIn<uint16_t> tau_ms;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<float> wind_out;
    Vars::RefOut<float> gust_out;
    Vars::RefOut<float> rain_out;
    Vars::RefOut<float> irrad_out;
    Vars::RefOut<bool>  fault;

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
    wind[0]   = _bind_input<float>(io, "cli_wind1_mps");
    wind[1]   = _bind_input<float>(io, "cli_wind2_mps");
    gust[0]   = _bind_input<float>(io, "cli_windGust1_mps");
    gust[1]   = _bind_input<float>(io, "cli_windGust2_mps");
    rain[0]   = _bind_input<float>(io, "cli_rain1_mmph");
    rain[1]   = _bind_input<float>(io, "cli_rain2_mmph");
    irrad[0]  = _bind_input<float>(io, "cli_irrad1_Wpm2");
    irrad[1]  = _bind_input<float>(io, "cli_irrad2_Wpm2");
    tau_ms    = _bind_input<uint16_t>(io, "cli_wxTau_ms");
    wind_out  = _bind_output<float>(io, "cli_wind_mps");
    gust_out  = _bind_output<float>(io, "cli_windGust_mps");
    rain_out  = _bind_output<float>(io, "cli_rain_mmph");
    irrad_out = _bind_output<float>(io, "cli_irrad_Wpm2");
    fault     = _bind_output<bool>(io, "alarm_wx_station");

    stations.attach(wind, gust, rain, irrad,
                    wind_out, gust_out, rain_out, irrad_out, fault,
                    0.001f * (float)tau_ms.get());
}

}  // namespace Site

#endif // SITE_PROGRAM_WX_SENSORS_H
