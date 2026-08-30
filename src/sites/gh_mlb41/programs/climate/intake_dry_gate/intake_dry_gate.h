/**
 * @file intake_dry_gate.h
 * @brief The declaration half of the `intake_dry_gate` POU: its rows, its
 *        block, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_INTAKE_DRY_GATE_H
#define SITE_PROGRAM_INTAKE_DRY_GATE_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

namespace Site {

using namespace Botanik;

/**
 * @brief Whether the air outside would dry the greenhouse if drawn in.
 *        Dew points, so it survives a cold damp morning reading 95% RH.
 */
class IntakeDryGate final : public Plc::Program {

public:

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<float> in_t;
    Vars::RefIn<float> in_rh;
    Vars::RefIn<float> ext_t;
    Vars::RefIn<float> ext_rh;
    Vars::RefIn<float> dry_margin;
    Vars::RefIn<float> vent_hyst;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<bool> dry_ok;

    /** @} */

private:

    /** @name VAR - function blocks
     *  @{ */

    /** @brief two dew points that converge, so a dead band */
    Plc::Hysteresis dry_gate_band;

    /** @} */

public:
    IntakeDryGate(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "intake_dry", Tasks::GROUP_SLOW, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
};

inline void IntakeDryGate::bind(Vars::Bank& io) {
    in_t       = _bind_input<float>(io, "cli_temp_hi");
    in_rh      = _bind_input<float>(io, "cli_rh_hi");
    ext_t      = _bind_input<float>(io, "cli_temp_ext");
    ext_rh     = _bind_input<float>(io, "cli_rh_ext");
    dry_margin = _bind_input<float>(io, "cli_ventDryMgn_degC");
    vent_hyst  = _bind_input<float>(io, "cli_ventHyst_degC");
    dry_ok     = _bind_output<bool>(io, "cli_intakeDryOk");

}

}  // namespace Site

#endif // SITE_PROGRAM_INTAKE_DRY_GATE_H
