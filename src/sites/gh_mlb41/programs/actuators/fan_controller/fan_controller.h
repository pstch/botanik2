/**
 * @file fan_controller.h
 * @brief The declaration half of the `fan_controller` POU: its rows, its
 *        blocks, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_FAN_CONTROLLER_H
#define SITE_PROGRAM_FAN_CONTROLLER_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

namespace Site {

using namespace Botanik;

/**
 * @brief The two destratification fans: three relayed speeds, driven by
 *        whichever of three demands wants the most.
 */
class FanController final : public Plc::Program {

public:

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<float>   dem_hyst;
    Vars::RefIn<float>   irr_dem;
    Vars::RefIn<float>   strat_dem;
    Vars::RefIn<float>   cool_dem;
    Vars::RefIn<uint8_t> irr_state;
    Vars::RefIn<bool>    screen_moving;
    Vars::RefIn<float>   low_dem;
    Vars::RefIn<float>   med_dem;
    Vars::RefIn<float>   high_dem;
    Vars::RefIn<uint8_t> phase;
    Vars::RefIn<float>   breeze_dem;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<bool>    speed1[2];
    Vars::RefOut<bool>    speed2[2];
    Vars::RefOut<bool>    speed3[2];
    Vars::RefOut<uint8_t> band;

    /** @} */

private:

    /** @name VAR - function blocks
     *  @{ */

    Plc::Staircase3 steps;

    /** @} */

    /** @name Sizes - what a body loops over
     *  @{ */

    static const uint8_t N_SPEED1 = 2;
    static const uint8_t N_SPEED2 = 2;
    static const uint8_t N_SPEED3 = 2;

    /** @} */

    Vars::VarRef _watch_rows[5];  ///< swept for validity before each activation

public:
    FanController(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "fans", Tasks::GROUP_SLOW, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;

private:

    /** @brief The three demands, fused. */
    float   _demand() const;
    /** @brief The speed @p demand asks for, given the one in force. */
    uint8_t _band(float demand);
    /** @brief Energise exactly one speed relay, or none. */
    void    _drive(uint8_t speed);
};

inline void FanController::bind(Vars::Bank& io) {
    dem_hyst      = io.resolve_in<float>("cli_fanDemHyst");
    irr_dem       = io.resolve_in<float>("cli_fanIrrDem");
    strat_dem     = io.resolve_in<float>("cli_stratDem");
    cool_dem      = io.resolve_in<float>("cli_coolDem");
    irr_state     = io.resolve_in<uint8_t>("irr_state");
    screen_moving = io.resolve_in<bool>("cli_screenMoving");
    low_dem       = io.resolve_in<float>("cli_fanLowDem");
    med_dem       = io.resolve_in<float>("cli_fanMedDem");
    high_dem      = io.resolve_in<float>("cli_fanHighDem");
    phase         = io.resolve_in<uint8_t>("cli_phase");
    breeze_dem    = io.resolve_in<float>("cli_breezeDem");
    speed1[0]     = io.resolve_out<bool>("cli_destrat1_speed1");
    speed1[1]     = io.resolve_out<bool>("cli_destrat2_speed1");
    speed2[0]     = io.resolve_out<bool>("cli_destrat1_speed2");
    speed2[1]     = io.resolve_out<bool>("cli_destrat2_speed2");
    speed3[0]     = io.resolve_out<bool>("cli_destrat1_speed3");
    speed3[1]     = io.resolve_out<bool>("cli_destrat2_speed3");
    band          = io.resolve_out<uint8_t>("cli_fanSpeed");

    // What the base sweeps before each activation, so an input that has
    // stopped being believable is DEGRADED without the law asking.
    _watch_rows[0] = strat_dem.r;
    _watch_rows[1] = cool_dem.r;
    _watch_rows[2] = irr_state.r;
    _watch_rows[3] = screen_moving.r;
    _watch_rows[4] = phase.r;
    set_inputs(_watch_rows, 5);
    set_health_row(io.resolve_out<uint8_t>("prog_fans"));
}

}  // namespace Site

#endif // SITE_PROGRAM_FAN_CONTROLLER_H
