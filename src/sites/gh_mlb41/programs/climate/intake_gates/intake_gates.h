/**
 * @file intake_gates.h
 * @brief The declaration half of the `intake_gates` POU: its rows, its
 *        blocks, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_INTAKE_GATES_H
#define SITE_PROGRAM_INTAKE_GATES_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

namespace Site {

using namespace Botanik;

/**
 * @brief Whether drawing crawlspace air would help, in either direction.
 *        Published rather than acted on: this law owns the reading, the
 *        intractors own the device.
 */
class IntakeGates final : public Plc::Program {

public:

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<float> inside;
    Vars::RefIn<float> intake;
    Vars::RefIn<float> cool_dem;
    Vars::RefIn<bool>  inside_qlt;
    Vars::RefIn<bool>  cool_dem_qlt;
    Vars::RefIn<float> temp_margin;
    Vars::RefIn<float> vent_hyst;
    Vars::RefIn<float> ground_dem;
    Vars::RefIn<float> dem_hyst;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<bool> intake_ok;
    Vars::RefOut<bool> ground_ok;

    /** @} */

private:

    /** @name VAR - function blocks
     *  @{ */

    Plc::Hysteresis cool_gate;    ///< the intake is cooler by the margin
    Plc::Hysteresis ground_gate;  ///< ...or warmer by it
    Plc::Hysteresis ground_band;  ///< and the ladder is asking for heat

    /** @} */

public:
    IntakeGates(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "intake_gates", Tasks::GROUP_FAST, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
};

inline void IntakeGates::bind(Vars::Bank& io) {
    inside      = _bind_input<float>(io, "cli_temp_in");
    intake      = _bind_input<float>(io, "cli_temp_cs");
    cool_dem    = _bind_input<float>(io, "cli_coolDem");
    inside_qlt  = _bind_input<bool>(io, "cli_temp_in_qlt");
    cool_dem_qlt = _bind_input<bool>(io, "cli_coolDem_qlt");
    temp_margin = _bind_input<float>(io, "cli_ventTempMgn_degC");
    vent_hyst   = _bind_input<float>(io, "cli_ventHyst_degC");
    ground_dem  = _bind_input<float>(io, "cli_groundHeatDem");
    dem_hyst    = _bind_input<float>(io, "cli_groundDemHyst");
    intake_ok   = _bind_output<bool>(io, "cli_intakeCoolOk");
    ground_ok   = _bind_output<bool>(io, "cli_groundHeatOk");

}

}  // namespace Site

#endif // SITE_PROGRAM_INTAKE_GATES_H
