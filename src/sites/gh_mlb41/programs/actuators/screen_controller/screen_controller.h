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

public:
    ScreenController(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "screen", Tasks::GROUP_SLOW, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;

private:

    /** @brief The traverse, rebuilt each scan from the parameters: a re-
     *  timed screen has to land on the next stroke, not the next boot. */
    Plc::ActuatorConfig _mechanics() const;
};

inline void ScreenController::bind(Vars::Bank& io) {
    cool_dem     = _bind_input<float>(io, "cli_coolDem");
    deploy_dem   = _bind_input<float>(io, "cli_thermalScreenDem");
    phase        = _bind_input<uint8_t>(io, "cli_phase");
    travel_s     = _bind_input<uint16_t>(io, "cli_screenTravel_sec");
    travel_tmo_s = _bind_input<uint16_t>(io, "cli_screenTmo_sec");
    screen       = _bind_output<bool>(io, "cli_screen");
    moving       = _bind_output<bool>(io, "cli_screenMoving");

}

}  // namespace Site

#endif // SITE_PROGRAM_SCREEN_CONTROLLER_H
