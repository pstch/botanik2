/**
 * @file irr_valves.cpp
 * @brief HAND-WRITTEN - the body of the `irr_valves` POU.
 */

#include "irr_valves.h"

namespace Site {

void IrrValves::on_bind() {
    for (uint8_t i = 0; i < 2; i++) {
        inlet_v[i].attach(inlet[i]);
        outlet_v[i].attach(outlet[i]);
    }
    for (uint8_t i = 0; i < N_TRAY_VALVE; i++) tray_v[i].attach(tray_valve[i]);
}

void IrrValves::run(ProgramContext& c) {
    (void)c;
    if (going_safe()) {
        for (uint8_t i = 0; i < 2; i++) {
            inlet_v[i].park();
            outlet_v[i].park();
        }
        for (uint8_t i = 0; i < N_TRAY_VALVE; i++) tray_v[i].park();
        return;
    }

    // Fill: inlet open, this tray open, outlet shut. Drain: inlet shut, tray
    // still open, outlet open. Every other tray is shut in both, so one cycle
    // can never water two.
    // Both default to IDLE/no-tray, which is a safe pattern rather than a
    // dangerous one - but it is a pattern nobody asked for, so say so.
    if (!state.valid() || !active.valid()) degraded();
    const IrrState st = (IrrState)state.get();
    const uint8_t  tray = (uint8_t)active.get();
    const bool filling  = (st == IRR_FILLING);
    const bool draining = (st == IRR_DRAINING);
    // SOAKING keeps the tray connected with both ends of the manifold shut, so
    // the dose sits where it was put and nothing moves.
    const bool soaking  = (st == IRR_SOAKING);
    // FLUSHING opens BOTH ends with no tray between them: fresh water runs the
    // length of the shared manifold and out through the drain, so what sat in
    // the line after a tray emptied is not what the next tray gets first.
    for (uint8_t i = 0; i < 2; i++) {
        inlet_v[i].set(filling || st == IRR_FLUSHING, true);
        outlet_v[i].set(draining || st == IRR_FLUSHING, true);
    }
    for (uint8_t i = 0; i < N_TRAY_VALVE; i++)
        tray_v[i].set((filling || soaking || draining) && (i + 1 == tray), true);
}

}  // namespace Site
