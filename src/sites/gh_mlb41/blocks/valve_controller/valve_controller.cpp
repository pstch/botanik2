/**
 * @file valve_controller.cpp
 * @brief HAND-WRITTEN - the valve block's out-of-line members.
 */

#include "valve_controller.h"

namespace Site {

void ValveController::bind(const ValveRefs& r) {
    _coil = r.coil;
    // The declared safe state IS the fail direction: a de-energised coil is
    // the only thing a dead controller guarantees, and the row already says
    // which way that leaves this valve.
    const Vars::Descriptor& d = r.coil.io->descriptor(r.coil.r.index);
    _fail_open = Vars::as_bool(d.type, d.def);
}

float ValveController::operator()(const ValveCmd& c) {
    if (c.mode == ValveCmd::DRIVE) {
        _coil.set(c.open, c.current);
        return c.open ? 1.0f : 0.0f;
    }
    if (c.mode == ValveCmd::PARK) {
        // A parked valve is a DECISION, not an estimate: the coil goes where a
        // dead controller would leave it, and that is as current as anything.
        _coil.set(_fail_open, true);
        return _fail_open ? 1.0f : 0.0f;
    }

    const float    duty      = Plc::limit(c.duty, 0.0f, 1.0f);
    const uint32_t period_ms = (c.period_ms == 0) ? 1u : c.period_ms;

    const uint32_t on_ms = (uint32_t)(duty * (float)period_ms);
    // A pulse under the minimum does not lift the seat, so it is not made:
    // rounding it up instead would deliver water nobody asked for.
    const uint32_t served = (on_ms < c.min_on_ms) ? 0 : on_ms;

    _phase_ms += c.dt_ms;
    while (_phase_ms >= period_ms) _phase_ms -= period_ms;

    _coil.set(_phase_ms < served, true);
    return (float)served / (float)period_ms;
}

}  // namespace Site
