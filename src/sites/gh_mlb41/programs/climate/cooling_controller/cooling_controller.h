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
    Vars::RefIn<float> ceiling;
    Vars::RefIn<float> floor;
    Vars::RefIn<float> target;
    Vars::RefIn<float> win_start;
    Vars::RefIn<float> win_full;
    Vars::RefIn<float> kp;
    Vars::RefIn<float> ti;
    Vars::RefIn<float> td;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

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
        : Program(ctx, "cooling", Tasks::GROUP_FAST, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
};

inline void CoolingController::bind(Vars::Bank& io) {
    inside      = _bind_input<float>(io, "cli_temp_in");
    ceiling     = _bind_input<float>(io, "cli_temp_hi");
    floor       = _bind_input<float>(io, "cli_temp_lo");
    target      = _bind_input<float>(io, "cli_targetTemp_degC");
    win_start   = _bind_input<float>(io, "cli_windowStartDem");
    win_full    = _bind_input<float>(io, "cli_windowFullDem");
    kp          = _bind_input<float>(io, "cli_coolKp");
    ti          = _bind_input<float>(io, "cli_coolTi_sec");
    td          = _bind_input<float>(io, "cli_coolTd_sec");
    demand      = _bind_output<float>(io, "cli_coolDem");
    window      = _bind_output<float>(io, "cli_windowDem");

    set_health_row(_bind_output<uint8_t>(io, "prog_cooling"));
}

}  // namespace Site

#endif // SITE_PROGRAM_COOLING_CONTROLLER_H
