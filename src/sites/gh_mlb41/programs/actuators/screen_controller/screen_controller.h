/**
 * @file screen_controller.h
 * @brief The declaration half of the `screen_controller` POU: its rows, its
 *        blocks, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_SCREEN_CONTROLLER_H
#define SITE_PROGRAM_SCREEN_CONTROLLER_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

namespace Site {

using namespace Botanik;

/**
 * @brief The thermal screen: one coil, deployed when the greenhouse is losing
 *        heat rather than gaining it.
 */
class ScreenController final : public Plc::Program {

public:

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<float>    cool_dem;
    Vars::RefIn<float>    deploy_dem;
    Vars::RefIn<uint8_t>  phase;
    Vars::RefIn<uint16_t> travel_s;
    Vars::RefIn<uint16_t> travel_tmo_s;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<bool> screen;
    Vars::RefOut<bool> moving;

    /** @} */

private:

    /** @name VAR - function blocks
     *  @{ */

    /** @brief where the fabric is, and whether it is travelling */
    Plc::Actuator fabric;

    /** @} */

    Vars::VarRef _watch_rows[2];  ///< swept for validity before each activation

public:
    ScreenController(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "screen", Tasks::GROUP_SLOW, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
    void observe(ProgramContext& c) override;  ///< reports in EVERY state

private:

    /** @brief The traverse, rebuilt each scan from the parameters: a re-
     *  timed screen has to land on the next stroke, not the next boot. */
    Plc::ActuatorConfig _mechanics() const;
};

inline void ScreenController::bind(Vars::Bank& io) {
    cool_dem     = io.resolve_in<float>("cli_coolDem");
    deploy_dem   = io.resolve_in<float>("cli_thermalScreenDem");
    phase        = io.resolve_in<uint8_t>("cli_phase");
    travel_s     = io.resolve_in<uint16_t>("cli_screenTravel_sec");
    travel_tmo_s = io.resolve_in<uint16_t>("cli_screenTmo_sec");
    screen       = io.resolve_out<bool>("cli_screen");
    moving       = io.resolve_out<bool>("cli_screenMoving");

    // What the base sweeps before each activation, so an input that has
    // stopped being believable is DEGRADED without the law asking.
    _watch_rows[0] = cool_dem.r;
    _watch_rows[1] = phase.r;
    set_inputs(_watch_rows, 2);
    set_health_row(io.resolve_out<uint8_t>("prog_screen"));
}

}  // namespace Site

#endif // SITE_PROGRAM_SCREEN_CONTROLLER_H
