/**
 * @file climate_watch.h
 * @brief The declaration half of the `climate_watch` POU: its rows, its
 *        blocks, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_CLIMATE_WATCH_H
#define SITE_PROGRAM_CLIMATE_WATCH_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

namespace Site {

using namespace Botanik;

/**
 * @brief The interior conditions, watched: the alarms that are a READING
 *        crossing a line rather than a law failing to hold one.
 */
class ClimateWatch final : public Plc::Program {

public:

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<float> temp;
    Vars::RefIn<float> margin;
    Vars::RefIn<bool>  temp_qlt;
    Vars::RefIn<bool>  margin_qlt;
    Vars::RefIn<float> strat;
    Vars::RefIn<float> too_hot;
    Vars::RefIn<float> too_cold;
    Vars::RefIn<float> humid_at;
    Vars::RefIn<float> strat_hi;
    Vars::RefIn<bool>  sns[4];

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<bool> hot;
    Vars::RefOut<bool> cold;
    Vars::RefOut<bool> humid;
    Vars::RefOut<bool> stratified;
    Vars::RefOut<bool> sensor;

    /** @} */

private:

    /** @name Sizes - what a body loops over
     *  @{ */

    static const uint8_t N_SNS = 4;

    /** @} */

public:
    ClimateWatch(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "climate_watch", Tasks::GROUP_SLOW, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
};

inline void ClimateWatch::bind(Vars::Bank& io) {
    temp       = _bind_input<float>(io, "cli_temp_hi");
    margin     = _bind_input<float>(io, "cli_dpMargin_degC");
    temp_qlt   = _bind_input<bool>(io, "cli_temp_hi_qlt");
    margin_qlt = _bind_input<bool>(io, "cli_dpMargin_degC_qlt");
    strat      = _bind_input<float>(io, "cli_dTStrat_degC");
    too_hot    = _bind_input<float>(io, "cli_tooHot_degC");
    too_cold   = _bind_input<float>(io, "cli_tooCold_degC");
    humid_at   = _bind_input<float>(io, "cli_dehumFull_degC");
    strat_hi   = _bind_input<float>(io, "cli_dTStratHigh_degC");
    sns[0]     = _bind_input<bool>(io, "alarm_sns_ceiling");
    sns[1]     = _bind_input<bool>(io, "alarm_sns_floor");
    sns[2]     = _bind_input<bool>(io, "alarm_sns_crawlspace");
    sns[3]     = _bind_input<bool>(io, "alarm_sns_exterior");
    hot        = _bind_output<bool>(io, "alarm_too_hot");
    cold       = _bind_output<bool>(io, "alarm_too_cold");
    humid      = _bind_output<bool>(io, "alarm_too_humid");
    stratified = _bind_output<bool>(io, "alarm_strat");
    sensor     = _bind_output<bool>(io, "alarm_sensor_fault");

}

}  // namespace Site

#endif // SITE_PROGRAM_CLIMATE_WATCH_H
