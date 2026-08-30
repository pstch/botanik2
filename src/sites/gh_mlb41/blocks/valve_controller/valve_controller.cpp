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

void ValveController::set(bool open, bool current) {
    _coil.set(open, current);
}

void ValveController::park() {
    // A parked valve is a DECISION, not an estimate: the coil goes where a
    // dead controller would leave it, and that is as current as anything.
    _coil.set(_fail_open, true);
}

float ValveController::operator()(float duty, uint32_t period_ms,
                                  uint32_t min_on_ms, uint32_t dt_ms) {
    duty = Plc::limit(duty, 0.0f, 1.0f);
    if (period_ms == 0) period_ms = 1;

    const uint32_t on_ms = (uint32_t)(duty * (float)period_ms);
    // A pulse under the minimum does not lift the seat, so it is not made:
    // rounding it up instead would deliver water nobody asked for.
    const uint32_t served = (on_ms < min_on_ms) ? 0 : on_ms;

    _phase_ms += dt_ms;
    while (_phase_ms >= period_ms) _phase_ms -= period_ms;

    _coil.set(_phase_ms < served, true);
    return (float)served / (float)period_ms;
}

}  // namespace Site
