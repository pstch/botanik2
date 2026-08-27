/**
 * @file destrat_controller.h
 * @brief The declaration half of the `destrat_controller` POU: its rows, its
 *        blocks, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_DESTRAT_CONTROLLER_H
#define SITE_PROGRAM_DESTRAT_CONTROLLER_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

namespace Site {

using namespace Botanik;

/**
 * @brief The destratification ladder: three bands with hysteresis on the
 *        ceiling-to- floor gap, published as a demand.
 */
class DestratController final : public Plc::Program {

public:

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<float> strat;
    Vars::RefIn<float> low;
    Vars::RefIn<float> med;
    Vars::RefIn<float> high;
    Vars::RefIn<float> hyst;
    Vars::RefIn<float> fan_low;
    Vars::RefIn<float> fan_med;
    Vars::RefIn<float> fan_high;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<float> demand;

    /** @} */

private:

    /** @name VAR - function blocks
     *  @{ */

    Plc::Staircase3 bands;

    /** @} */

public:
    DestratController(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "destrat", Tasks::GROUP_SLOW, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
};

inline void DestratController::bind(Vars::Bank& io) {
    strat    = _bind_input<float>(io, "cli_dTStrat_degC");
    low      = _bind_input<float>(io, "cli_dTStratLow_degC");
    med      = _bind_input<float>(io, "cli_dTStratMed_degC");
    high     = _bind_input<float>(io, "cli_dTStratHigh_degC");
    hyst     = _bind_input<float>(io, "cli_dTStratHyst_degC");
    fan_low  = _bind_input<float>(io, "cli_fanLowDem");
    fan_med  = _bind_input<float>(io, "cli_fanMedDem");
    fan_high = _bind_input<float>(io, "cli_fanHighDem");
    demand   = _bind_output<float>(io, "cli_stratDem");

    set_health_row(_bind_output<uint8_t>(io, "prog_destrat"));
}

}  // namespace Site

#endif // SITE_PROGRAM_DESTRAT_CONTROLLER_H
