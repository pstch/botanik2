/**
 * @file day_phase_controller.h
 * @brief The declaration half of the `day_phase_controller` POU: its rows, its
 *        blocks, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_DAY_PHASE_CONTROLLER_H
#define SITE_PROGRAM_DAY_PHASE_CONTROLLER_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

namespace Site {

using namespace Botanik;

/**
 * @brief The day's phase, and the temperature target it puts in force.
 */
class DayPhaseController final : public Plc::Program {

public:

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<float>   inside;
    Vars::RefIn<int16_t> from_rise;
    Vars::RefIn<int16_t> from_set;
    Vars::RefIn<float>   cold_bank_trg;
    Vars::RefIn<float>   heat_bank_trg;
    Vars::RefIn<int16_t> cold_bank_rise;
    Vars::RefIn<int16_t> breeze_rise;
    Vars::RefIn<int16_t> day_rise;
    Vars::RefIn<int16_t> heat_bank_set;
    Vars::RefIn<int16_t> dusk_set;
    Vars::RefIn<int16_t> night_set;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<uint8_t> phase;
    Vars::RefOut<bool>    cold_arm;
    Vars::RefOut<bool>    heat_arm;

    /** @} */

    /** @name VAR_IN_OUT
     *  @{ */

    Vars::RefInOut<float> prev_day_max;
    Vars::RefInOut<float> prev_night_min;

    /** @} */

private:

    /** @name VAR - what this law remembers
     *  @{ */

    DayPhase _phase = PHASE_NIGHT;
    /** @brief The interior extremes since the current day and night began.
     *  What COLD_BANK and HEAT_BANK are gated on: pre-cool only after a hot
     *  day, bank heat only after a cold night. */
    float    _day_max = 0.0f;
    float    _night_min = 0.0f;
    bool     _seeded = false;

    /** @} */

    Vars::VarRef _watch_rows[5];  ///< swept for validity before each activation

public:
    DayPhaseController(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "day_phase", Tasks::GROUP_SLOW, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;

private:

    /** @brief The phase two offsets land in, with the two BANK phases
     *  skipped when yesterday did not arm them. */
    DayPhase _phase_at(int16_t from_rise, int16_t from_set) const;
};

inline void DayPhaseController::bind(Vars::Bank& io) {
    inside         = io.resolve_in<float>("cli_temp_in");
    from_rise      = io.resolve_in<int16_t>("sol_fromRise_min");
    from_set       = io.resolve_in<int16_t>("sol_fromSet_min");
    cold_bank_trg  = io.resolve_in<float>("cli_coldBankTrg_degC");
    heat_bank_trg  = io.resolve_in<float>("cli_heatBankTrg_degC");
    cold_bank_rise = io.resolve_in<int16_t>("sol_coldBankRise_min");
    breeze_rise    = io.resolve_in<int16_t>("sol_breezeRise_min");
    day_rise       = io.resolve_in<int16_t>("sol_dayRise_min");
    heat_bank_set  = io.resolve_in<int16_t>("sol_heatBankSet_min");
    dusk_set       = io.resolve_in<int16_t>("sol_duskSet_min");
    night_set      = io.resolve_in<int16_t>("sol_nightSet_min");
    phase          = io.resolve_out<uint8_t>("cli_phase");
    cold_arm       = io.resolve_out<bool>("cli_coldBankArm");
    heat_arm       = io.resolve_out<bool>("cli_heatBankArm");
    prev_day_max   = io.resolve_inout<float>("cli_prevDayMax_degC");
    prev_night_min = io.resolve_inout<float>("cli_prevNightMin_degC");

    // What the base sweeps before each activation, so an input that has
    // stopped being believable is DEGRADED without the law asking.
    _watch_rows[0] = inside.r;
    _watch_rows[1] = from_rise.r;
    _watch_rows[2] = from_set.r;
    _watch_rows[3] = prev_day_max.r;
    _watch_rows[4] = prev_night_min.r;
    set_inputs(_watch_rows, 5);
    set_health_row(io.resolve_out<uint8_t>("prog_day_phase"));
}

}  // namespace Site

#endif // SITE_PROGRAM_DAY_PHASE_CONTROLLER_H
