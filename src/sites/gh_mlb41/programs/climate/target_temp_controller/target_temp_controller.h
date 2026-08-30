/**
 * @file target_temp_controller.h
 * @brief The declaration half of the `target_temp_controller` POU: its rows, its
 *        blocks, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_TARGET_TEMP_CONTROLLER_H
#define SITE_PROGRAM_TARGET_TEMP_CONTROLLER_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

namespace Site {

using namespace Botanik;

/**
 * @brief The temperature the greenhouse is being held to: the base setting,
 *        shifted by whichever bank phase is both IN FORCE and ARMED.
 */
class TargetTempController final : public Plc::Program {

public:

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<float>    t_min;
    Vars::RefIn<float>    t_max;
    Vars::RefIn<uint8_t>  phase;
    Vars::RefIn<bool>     phase_qlt;
    Vars::RefIn<bool>     cold_arm;
    Vars::RefIn<bool>     heat_arm;
    Vars::RefIn<float>    cool_set;
    Vars::RefIn<float>    cold_bank_ofs;
    Vars::RefIn<float>    heat_bank_ofs;
    Vars::RefIn<uint16_t> tau;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<float> target;

    /** @} */

private:

    /** @name VAR - function blocks
     *  @{ */

    Plc::Ewma smooth;  ///< eases a phase boundary over cli_targetTau_s

    /** @} */

public:
    TargetTempController(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "target_temp", BOTANIK_SITE_CONTROL_MS, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
};

inline void TargetTempController::bind(Vars::Bank& io) {
    t_min         = _bind_input<float>(io, "cli_targetMin_degC");
    t_max         = _bind_input<float>(io, "cli_targetMax_degC");
    phase         = _bind_input<uint8_t>(io, "cli_phase");
    phase_qlt     = _bind_input<bool>(io, "cli_phase_qlt");
    cold_arm      = _bind_input<bool>(io, "cli_coldBankArm");
    heat_arm      = _bind_input<bool>(io, "cli_heatBankArm");
    cool_set      = _bind_input<float>(io, "cli_coolSet_degC");
    cold_bank_ofs = _bind_input<float>(io, "cli_coldBankOfs_degC");
    heat_bank_ofs = _bind_input<float>(io, "cli_heatBankOfs_degC");
    tau           = _bind_input<uint16_t>(io, "cli_targetTau_s");
    target        = _bind_output<float>(io, "cli_targetTemp_degC");

}

}  // namespace Site

#endif // SITE_PROGRAM_TARGET_TEMP_CONTROLLER_H
