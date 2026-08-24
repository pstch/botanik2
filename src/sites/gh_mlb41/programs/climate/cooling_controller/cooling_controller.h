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

    Vars::RefIn<float> dem_hyst;
    Vars::RefIn<float> inside;
    Vars::RefIn<float> ceiling;
    Vars::RefIn<float> floor;
    Vars::RefIn<float> target;
    Vars::RefIn<float> intake;
    Vars::RefIn<float> temp_margin;
    Vars::RefIn<float> vent_hyst;
    Vars::RefIn<float> ground_dem;
    Vars::RefIn<float> win_start;
    Vars::RefIn<float> win_full;
    Vars::RefIn<float> kp;
    Vars::RefIn<float> ti;
    Vars::RefIn<float> td;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<float> demand;
    Vars::RefOut<bool>  intake_ok;
    Vars::RefOut<bool>  ground_ok;
    Vars::RefOut<float> window;

    /** @} */

private:

    /** @name VAR - function blocks
     *  @{ */

    Plc::Pid        pid;
    Plc::Hysteresis cool_gate;    ///< the intake is cooler by the margin
    Plc::Hysteresis ground_gate;  ///< ...or warmer by it
    Plc::Hysteresis ground_band;  ///< and the ladder is asking for heat

    /** @} */

    Vars::VarRef _watch_rows[5];  ///< swept for validity before each activation

public:
    CoolingController(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "cooling", Tasks::GROUP_FAST, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;

private:

    /** @brief Whether drawing crawlspace air would help, in either
     *  direction. Published rather than acted on: this law owns the
     *  reading, the intractors own the device. */
    void _intake_gates(ProgramContext& c, float t_in, float dem);
};

inline void CoolingController::bind(Vars::Bank& io) {
    dem_hyst    = io.resolve_in<float>("cli_groundDemHyst");
    inside      = io.resolve_in<float>("cli_temp_in");
    ceiling     = io.resolve_in<float>("cli_temp_hi");
    floor       = io.resolve_in<float>("cli_temp_lo");
    target      = io.resolve_in<float>("cli_targetTemp_degC");
    intake      = io.resolve_in<float>("cli_temp_cs");
    temp_margin = io.resolve_in<float>("cli_ventTempMgn_degC");
    vent_hyst   = io.resolve_in<float>("cli_ventHyst_degC");
    ground_dem  = io.resolve_in<float>("cli_groundHeatDem");
    win_start   = io.resolve_in<float>("cli_windowStartDem");
    win_full    = io.resolve_in<float>("cli_windowFullDem");
    kp          = io.resolve_in<float>("cli_coolKp");
    ti          = io.resolve_in<float>("cli_coolTi_sec");
    td          = io.resolve_in<float>("cli_coolTd_sec");
    demand      = io.resolve_out<float>("cli_coolDem");
    intake_ok   = io.resolve_out<bool>("cli_intakeCoolOk");
    ground_ok   = io.resolve_out<bool>("cli_groundHeatOk");
    window      = io.resolve_out<float>("cli_windowDem");

    // What the base sweeps before each activation, so an input that has
    // stopped being believable is DEGRADED without the law asking.
    _watch_rows[0] = inside.r;
    _watch_rows[1] = ceiling.r;
    _watch_rows[2] = floor.r;
    _watch_rows[3] = target.r;
    _watch_rows[4] = intake.r;
    set_inputs(_watch_rows, 5);
    set_health_row(io.resolve_out<uint8_t>("prog_cooling"));
}

}  // namespace Site

#endif // SITE_PROGRAM_COOLING_CONTROLLER_H
