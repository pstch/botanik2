/**
 * @file irr_valves.h
 * @brief The declaration half of the `irr_valves` POU: its rows, its
 *        blocks, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_IRR_VALVES_H
#define SITE_PROGRAM_IRR_VALVES_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

#include "../../../blocks/valve_controller/valve_controller.h"

namespace Site {

using namespace Botanik;

/**
 * @brief The irrigation manifold: which valves each state of the cycle opens.
 */
class IrrValves final : public Plc::Program {

public:

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<uint8_t> state;
    Vars::RefIn<uint8_t> active;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<bool> inlet[2];
    Vars::RefOut<bool> outlet[2];
    Vars::RefOut<bool> tray_valve[8];

    /** @} */

private:

    /** @name VAR - function blocks
     *  @{ */

    ValveController inlet_v[2];  ///< two contactors on one manifold
    ValveController outlet_v[2];
    ValveController tray_v[8];

    /** @} */

    /** @name Sizes - what a body loops over
     *  @{ */

    static const uint8_t N_INLET      = 2;
    static const uint8_t N_OUTLET     = 2;
    static const uint8_t N_TRAY_VALVE = 8;
    static const uint8_t N_INLET_V    = 2;
    static const uint8_t N_OUTLET_V   = 2;
    static const uint8_t N_TRAY_V     = 8;

    /** @} */

public:
    IrrValves(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "irr_valves", Tasks::GROUP_SLOW, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
    void on_bind();  ///< wire a block to a row it was handed
};

inline void IrrValves::bind(Vars::Bank& io) {
    state         = _bind_input<uint8_t>(io, "irr_state");
    active        = _bind_input<uint8_t>(io, "irr_activeTray");
    inlet[0]      = _bind_output<bool>(io, "irr_input_ro1");
    inlet[1]      = _bind_output<bool>(io, "irr_input_ro2");
    outlet[0]     = _bind_output<bool>(io, "irr_output_ro1");
    outlet[1]     = _bind_output<bool>(io, "irr_output_ro2");
    tray_valve[0] = _bind_output<bool>(io, "irr_tray1");
    tray_valve[1] = _bind_output<bool>(io, "irr_tray2");
    tray_valve[2] = _bind_output<bool>(io, "irr_tray3");
    tray_valve[3] = _bind_output<bool>(io, "irr_tray4");
    tray_valve[4] = _bind_output<bool>(io, "irr_tray5");
    tray_valve[5] = _bind_output<bool>(io, "irr_tray6");
    tray_valve[6] = _bind_output<bool>(io, "irr_tray7");
    tray_valve[7] = _bind_output<bool>(io, "irr_tray8");

    for (uint8_t i = 0; i < 2; i++) {
        inlet_v[i].bind(ValveRefs{ inlet[i] });
        outlet_v[i].bind(ValveRefs{ outlet[i] });
    }
    for (uint8_t i = 0; i < N_TRAY_VALVE; i++)
        tray_v[i].bind(ValveRefs{ tray_valve[i] });
}

}  // namespace Site

#endif // SITE_PROGRAM_IRR_VALVES_H
