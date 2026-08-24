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

    Vars::VarRef _watch_rows[3];  ///< swept for validity before each activation

public:
    TargetTempController(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "target_temp", Tasks::GROUP_SLOW, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
};

inline void TargetTempController::bind(Vars::Bank& io) {
    t_min         = io.resolve_in<float>("cli_targetMin_degC");
    t_max         = io.resolve_in<float>("cli_targetMax_degC");
    phase         = io.resolve_in<uint8_t>("cli_phase");
    cold_arm      = io.resolve_in<bool>("cli_coldBankArm");
    heat_arm      = io.resolve_in<bool>("cli_heatBankArm");
    cool_set      = io.resolve_in<float>("cli_coolSet_degC");
    cold_bank_ofs = io.resolve_in<float>("cli_coldBankOfs_degC");
    heat_bank_ofs = io.resolve_in<float>("cli_heatBankOfs_degC");
    tau           = io.resolve_in<uint16_t>("cli_targetTau_s");
    target        = io.resolve_out<float>("cli_targetTemp_degC");

    // What the base sweeps before each activation, so an input that has
    // stopped being believable is DEGRADED without the law asking.
    _watch_rows[0] = phase.r;
    _watch_rows[1] = cold_arm.r;
    _watch_rows[2] = heat_arm.r;
    set_inputs(_watch_rows, 3);
    set_health_row(io.resolve_out<uint8_t>("prog_target_temp"));
}

}  // namespace Site

#endif // SITE_PROGRAM_TARGET_TEMP_CONTROLLER_H
