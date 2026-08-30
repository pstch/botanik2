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
    dem_hyst      = _bind_input<float>(io, "cli_fanDemHyst");
    irr_dem       = _bind_input<float>(io, "cli_fanIrrDem");
    strat_dem     = _bind_input<float>(io, "cli_stratDem");
    cool_dem      = _bind_input<float>(io, "cli_coolDem");
    irr_state     = _bind_input<uint8_t>(io, "irr_state");
    low_dem       = _bind_input<float>(io, "cli_fanLowDem");
    med_dem       = _bind_input<float>(io, "cli_fanMedDem");
    high_dem      = _bind_input<float>(io, "cli_fanHighDem");
    phase         = _bind_input<uint8_t>(io, "cli_phase");
    breeze_dem    = _bind_input<float>(io, "cli_breezeDem");
    speed1[0]     = _bind_output<bool>(io, "cli_destrat1_speed1");
    speed1[1]     = _bind_output<bool>(io, "cli_destrat2_speed1");
    speed2[0]     = _bind_output<bool>(io, "cli_destrat1_speed2");
    speed2[1]     = _bind_output<bool>(io, "cli_destrat2_speed2");
    speed3[0]     = _bind_output<bool>(io, "cli_destrat1_speed3");
    speed3[1]     = _bind_output<bool>(io, "cli_destrat2_speed3");
    band          = _bind_output<uint8_t>(io, "cli_fanSpeed");

}

}  // namespace Site

#endif // SITE_PROGRAM_FAN_CONTROLLER_H
