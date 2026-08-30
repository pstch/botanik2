/**
 * @file cooling_controller.h
 * @brief The declaration half of the `cooling_controller` POU: its rows, its
 *        blocks, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_COOLING_CONTROLLER_H
#define SITE_PROGRAM_COOLING_CONTROLLER_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

namespace Site {

using namespace Botanik;

/**
 * @brief The cooling ladder: a PID on the interior temperature against the
 *        phase's target, published as one signed demand.
 */
class CoolingController final : public Plc::Program {

public:

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<float> inside;
    Vars::RefIn<float> target;
    Vars::RefIn<float> win_start;
    Vars::RefIn<float> win_full;
    Vars::RefIn<float> kp;
    Vars::RefIn<float> ti;
    Vars::RefIn<float> td;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefIn<bool>  inside_qlt;

    Vars::RefOut<bool>  demand_qlt;

    Vars::RefOut<float> demand;
    Vars::RefOut<float> window;

    /** @} */

private:

    /** @name VAR - function blocks
     *  @{ */

    Plc::Pid        pid;

    /** @} */

public:
    CoolingController(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "cooling", BOTANIK_SITE_SENSE_MS, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
};

inline void CoolingController::bind(Vars::Bank& io) {
    inside      = _bind_input<float>(io, "cli_temp_in");
    target      = _bind_input<float>(io, "cli_targetTemp_degC");
    win_start   = _bind_input<float>(io, "cli_windowStartDem");
    win_full    = _bind_input<float>(io, "cli_windowFullDem");
    kp          = _bind_input<float>(io, "cli_coolKp");
    ti          = _bind_input<float>(io, "cli_coolTi_sec");
    td          = _bind_input<float>(io, "cli_coolTd_sec");
    demand      = _bind_output<float>(io, "cli_coolDem");
    demand_qlt  = _bind_output<bool>(io, "cli_coolDem_qlt");
    inside_qlt  = _bind_input<bool>(io, "cli_temp_in_qlt");
    window      = _bind_output<float>(io, "cli_windowDem");

}

}  // namespace Site

#endif // SITE_PROGRAM_COOLING_CONTROLLER_H
