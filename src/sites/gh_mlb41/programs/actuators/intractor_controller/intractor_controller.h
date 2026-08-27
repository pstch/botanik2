/**
 * @file intractor_controller.h
 * @brief The declaration half of the `intractor_controller` POU: its rows, its
 *        blocks, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_INTRACTOR_CONTROLLER_H
#define SITE_PROGRAM_INTRACTOR_CONTROLLER_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

namespace Site {

using namespace Botanik;

/**
 * @brief The two intractors: variable-speed fans drawing air from under the
 *        floor, driven by whichever of three demands wants the most.
 */
class IntractorController final : public Plc::Program {

public:

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<float> cool_dem;
    Vars::RefIn<float> rot_vent;
    Vars::RefIn<float> humid_dem;
    Vars::RefIn<float> ground_speed;
    Vars::RefIn<float> start_dem;
    Vars::RefIn<float> full_dem;
    Vars::RefIn<bool>  cool_ok;
    Vars::RefIn<bool>  ground_ok;
    Vars::RefIn<bool>  dry_ok;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<float> intr[2];

    /** @} */

private:

    /** @name VAR - what this law remembers
     *  @{ */

    /** @brief Where these go when this law is not acting, off the row's own
     *  declared safe value. NOT stopped: some air is always wanted. */
    float _safe_speed = 0.0f;

    /** @} */

    /** @name Sizes - what a body loops over
     *  @{ */

    static const uint8_t N_INTR = 2;

    /** @} */

public:
    IntractorController(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "intractors", Tasks::GROUP_SLOW, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
    void on_bind();  ///< wire a block to a row it was handed
};

inline void IntractorController::bind(Vars::Bank& io) {
    cool_dem     = _bind_input<float>(io, "cli_coolDem");
    rot_vent     = _bind_input<float>(io, "cli_rotVentDem");
    humid_dem    = _bind_input<float>(io, "cli_humidDem");
    ground_speed = _bind_input<float>(io, "cli_groundSpeed");
    start_dem    = _bind_input<float>(io, "cli_intakeStartDem");
    full_dem     = _bind_input<float>(io, "cli_intakeFullDem");
    cool_ok      = _bind_input<bool>(io, "cli_intakeCoolOk");
    ground_ok    = _bind_input<bool>(io, "cli_groundHeatOk");
    dry_ok       = _bind_input<bool>(io, "cli_intakeDryOk");
    intr[0]      = _bind_output<float>(io, "cli_intr1");
    intr[1]      = _bind_output<float>(io, "cli_intr2");

    set_health_row(_bind_output<uint8_t>(io, "prog_intractors"));
    on_bind();
}

}  // namespace Site

#endif // SITE_PROGRAM_INTRACTOR_CONTROLLER_H
