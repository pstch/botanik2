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
    DayPhase _phase_at(int16_t since_rise, int16_t since_set) const;
};

inline void DayPhaseController::bind(Vars::Bank& io) {
    inside         = _bind_input<float>(io, "cli_temp_in");
    from_rise      = _bind_input<int16_t>(io, "sol_fromRise_min");
    from_set       = _bind_input<int16_t>(io, "sol_fromSet_min");
    cold_bank_trg  = _bind_input<float>(io, "cli_coldBankTrg_degC");
    heat_bank_trg  = _bind_input<float>(io, "cli_heatBankTrg_degC");
    cold_bank_rise = _bind_input<int16_t>(io, "sol_coldBankRise_min");
    breeze_rise    = _bind_input<int16_t>(io, "sol_breezeRise_min");
    day_rise       = _bind_input<int16_t>(io, "sol_dayRise_min");
    heat_bank_set  = _bind_input<int16_t>(io, "sol_heatBankSet_min");
    dusk_set       = _bind_input<int16_t>(io, "sol_duskSet_min");
    night_set      = _bind_input<int16_t>(io, "sol_nightSet_min");
    phase          = _bind_output<uint8_t>(io, "cli_phase");
    cold_arm       = _bind_output<bool>(io, "cli_coldBankArm");
    heat_arm       = _bind_output<bool>(io, "cli_heatBankArm");
    prev_day_max   = _bind_inout<float>(io, "cli_prevDayMax_degC");
    prev_night_min = _bind_inout<float>(io, "cli_prevNightMin_degC");

    set_health_row(_bind_output<uint8_t>(io, "prog_day_phase"));
}

}  // namespace Site

#endif // SITE_PROGRAM_DAY_PHASE_CONTROLLER_H
