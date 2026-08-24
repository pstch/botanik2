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

    Vars::VarRef _watch_rows[6];  ///< swept for validity before each activation

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
    cool_dem     = io.resolve_in<float>("cli_coolDem");
    rot_vent     = io.resolve_in<float>("cli_rotVentDem");
    humid_dem    = io.resolve_in<float>("cli_humidDem");
    ground_speed = io.resolve_in<float>("cli_groundSpeed");
    start_dem    = io.resolve_in<float>("cli_intakeStartDem");
    full_dem     = io.resolve_in<float>("cli_intakeFullDem");
    cool_ok      = io.resolve_in<bool>("cli_intakeCoolOk");
    ground_ok    = io.resolve_in<bool>("cli_groundHeatOk");
    dry_ok       = io.resolve_in<bool>("cli_intakeDryOk");
    intr[0]      = io.resolve_out<float>("cli_intr1");
    intr[1]      = io.resolve_out<float>("cli_intr2");

    // What the base sweeps before each activation, so an input that has
    // stopped being believable is DEGRADED without the law asking.
    _watch_rows[0] = cool_dem.r;
    _watch_rows[1] = rot_vent.r;
    _watch_rows[2] = humid_dem.r;
    _watch_rows[3] = cool_ok.r;
    _watch_rows[4] = ground_ok.r;
    _watch_rows[5] = dry_ok.r;
    set_inputs(_watch_rows, 6);
    set_health_row(io.resolve_out<uint8_t>("prog_intractors"));
    on_bind();
}

}  // namespace Site

#endif // SITE_PROGRAM_INTRACTOR_CONTROLLER_H
