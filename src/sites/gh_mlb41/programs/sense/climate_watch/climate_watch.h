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

    Vars::VarRef _watch_rows[7];  ///< swept for validity before each activation

public:
    ClimateWatch(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "climate_watch", Tasks::GROUP_SLOW, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
    void observe(ProgramContext& c) override;  ///< reports in EVERY state
};

inline void ClimateWatch::bind(Vars::Bank& io) {
    temp       = io.resolve_in<float>("cli_temp_hi");
    margin     = io.resolve_in<float>("cli_dpMargin_degC");
    strat      = io.resolve_in<float>("cli_dTStrat_degC");
    too_hot    = io.resolve_in<float>("cli_tooHot_degC");
    too_cold   = io.resolve_in<float>("cli_tooCold_degC");
    humid_at   = io.resolve_in<float>("cli_dehumFull_degC");
    strat_hi   = io.resolve_in<float>("cli_dTStratHigh_degC");
    sns[0]     = io.resolve_in<bool>("alarm_sns_ceiling");
    sns[1]     = io.resolve_in<bool>("alarm_sns_floor");
    sns[2]     = io.resolve_in<bool>("alarm_sns_crawlspace");
    sns[3]     = io.resolve_in<bool>("alarm_sns_exterior");
    hot        = io.resolve_out<bool>("alarm_too_hot");
    cold       = io.resolve_out<bool>("alarm_too_cold");
    humid      = io.resolve_out<bool>("alarm_too_humid");
    stratified = io.resolve_out<bool>("alarm_strat");
    sensor     = io.resolve_out<bool>("alarm_sensor_fault");

    // What the base sweeps before each activation, so an input that has
    // stopped being believable is DEGRADED without the law asking.
    _watch_rows[0] = temp.r;
    _watch_rows[1] = margin.r;
    _watch_rows[2] = strat.r;
    _watch_rows[3] = sns[0].r;
    _watch_rows[4] = sns[1].r;
    _watch_rows[5] = sns[2].r;
    _watch_rows[6] = sns[3].r;
    set_inputs(_watch_rows, 7);
    set_health_row(io.resolve_out<uint8_t>("prog_climate_watch"));
}

}  // namespace Site

#endif // SITE_PROGRAM_CLIMATE_WATCH_H
