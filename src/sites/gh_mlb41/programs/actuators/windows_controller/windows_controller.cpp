/**
 * @file windows_controller.cpp
 * @brief HAND-WRITTEN - the body of the `windows` POU.
 */

#include <cmath>

#include "windows_controller.h"

namespace Site {

void WindowsController::on_bind() {
    // The safe opening is the row's own default, the way a valve takes its fail
    // direction from its coil row: one place declares where this actuator goes
    // when nobody is driving it.
    const Vars::Descriptor& d = tgt[0].io->descriptor(tgt[0].r.index);
    _safe_open = Vars::as_f32(d.type, d.def);
}

Plc::ActuatorConfig WindowsController::_mechanics() const {
    const uint32_t travel = 1000u * travel_s.get();
    const uint32_t tmo    = 1000u * tmo_s.get();
    // The safe time is the travel time plus the allowance for reaching the
    // stop, so the difference IS the seating run rather than a second figure
    // to keep in step with the first.
    return Plc::ActuatorConfig(travel, travel, 0.02f,
                               (tmo > travel) ? (tmo - travel) : 2000u);
}

int8_t WindowsController::_owner(float target,
                                 const Plc::ActuatorConfig& cfg) const {
    // A stroke is committed: whoever is moving keeps the bus until it stops,
    // or the next selection would leave a coil energised with nothing stepping
    // it down.
    for (uint8_t i = 0; i < N_ACT; i++)
        if (act[i].moving()) return (int8_t)i;
    for (uint8_t i = 0; i < N_ACT; i++) {
        if (!act[i].known()) return (int8_t)i;
        if (fabsf(target - act[i].position()) > cfg.deadband) return (int8_t)i;
    }
    return -1;
}

void WindowsController::run(ProgramContext& c) {
    if (c.first_scan) {
        // Where the rams actually are, nothing here knows: control was away
        // and the coils were dropped mid-stroke for all we can tell. Forget
        // the estimate and let the next command datum on a stop.
        for (uint8_t i = 0; i < N_ACT; i++) act[i].home();
    }

    const Plc::ActuatorConfig cfg = _mechanics();

    // Stage 1, decided upstream: the ROWS, never a verdict of this law's own.
    const bool  shut_in = window_lock.get();
    const float ceiling = Plc::limit(cap.get(), 0.0f, 1.0f);

    if (shut_in) {
        // Stage 1 of the arbitration, and absolute: rain and wind shut the roof
        // whatever else is true, including the safe state below.
        _target = 0.0f;
    } else {
        // The cascade STOPS here. An unbelievable demand reads as the row's
        // declared opening, which is a real position and the one the roof parks
        // at - so the law runs its normal path on it rather than branching.
        //
        // The coils and the position rows stay believable either way. A coil is
        // a command, not an estimate, and a ram's measured position is a fact
        // about the ram whatever asked it to move.
        _target = Plc::limit(window_dem.get(), 0.0f, 1.0f);
    }
    // ...and the wind's ceiling over all of it: a stiff wind is a reason to
    // open less, whatever the ladder asked for.
    if (_target > ceiling) _target = ceiling;

    const int8_t owner = _owner(_target, cfg);
    if (owner >= 0) act[owner].step(_target, cfg, c.dt_ms);

    for (uint8_t i = 0; i < N_ACT; i++) {
        opn[i].set(act[i].open_cmd(), true);
        clo[i].set(act[i].close_cmd(), true);
    }
}

void WindowsController::observe(ProgramContext&) {
    // One target for all four - they are one roof - against each ram's own
    // position estimate, which is what says how far behind the sequence is.
    for (uint8_t i = 0; i < N_ACT; i++) {
        tgt[i].set(_target, true);
        if (act[i].known()) pos[i].set(act[i].position(), true);
        else pos[i].invalidate();
    }
}

}  // namespace Site
