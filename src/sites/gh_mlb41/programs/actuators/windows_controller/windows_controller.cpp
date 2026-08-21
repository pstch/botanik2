/**
 * @file windows_controller.cpp
 * @brief HAND-WRITTEN - the body of the `windows` POU.
 */

#include <cmath>

#include "windows_controller.h"

#include "../../../vars.h"

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

bool WindowsController::_shut_in(ProgramContext& c) {
    // A weather station that has stopped answering is not "no rain". Both rows
    // fall back to 0, which reads exactly like fine weather, so the QUALITY is
    // what decides and an unmeasured sky shuts the roof.
    const bool blind = !rain_mmph.valid() ||
                       !wind_mps.valid();
    if (blind) degraded();

    // An OVERRUN on the RAIN: the roof stays shut for the declared time after
    // the last drop, because a shower that has just stopped is about to be one
    // that has not. Being BLIND is not a shower - it is a live interlock that
    // holds while it lasts and lets go the moment the station answers, where
    // feeding it the overrun cost the plant a quarter of an hour of ventilation
    // after every restart.
    const bool raining =
        rain_for(rain_mmph.get() > 0.0f, 1000u * rain_hold_s.get(),
                 c.dt_ms) || blind;

    // The GUST, not the average: what tears a window off its ram is the peak,
    // and the station's own window is the hysteresis - no dwell needed here.
    const float gust = gust_mps.get();
    const float mean = wind_mps.get();
    const bool blowing =
        blind || ((gust > mean ? gust : mean) >= wind_shut.get());

    rain_lock.set(raining);
    wind_lock.set(blowing);
    // READ BACK, never the local verdicts: each row is what an operator can
    // force, and a lockout the law then ignored would be a lie on the page.
    // Forcing one is also the only way to test the roof's stage-1 arbitration
    // without waiting for weather.
    window_lock.set(rain_lock.get() || wind_lock.get() || supply_lock.get());
    return window_lock.get();
}

int8_t WindowsController::_owner(float target,
                                 const Plc::ActuatorConfig& cfg) const {
    // A stroke is committed: whoever is moving keeps the bus until it stops,
    // or the next selection would leave a coil energised with nothing stepping
    // it down.
    for (uint8_t i = 0; i < WINDOWS; i++)
        if (act[i].moving()) return (int8_t)i;
    for (uint8_t i = 0; i < WINDOWS; i++) {
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
        for (uint8_t i = 0; i < WINDOWS; i++) act[i].home();
        rain_for.reset();
    }

    const Plc::ActuatorConfig cfg = _mechanics();

    // ALWAYS, and before anything else: it publishes the two lockout rows, and
    // an observation does not stop being made because this law is parked.
    const bool shut_in = _shut_in(c);

    if (shut_in) {
        // Stage 1 of the arbitration, and absolute: rain and wind shut the roof
        // whatever else is true, including the safe state below.
        _target = 0.0f;
    } else if (inhibit_requested()) {
        _target = _safe_open;
    } else if (!window_dem.valid()) {
        degraded();
        _target = _safe_open;
    } else {
        _target = Plc::limit(window_dem.get(), 0.0f, 1.0f);
    }

    const int8_t owner = _owner(_target, cfg);
    if (owner >= 0) act[owner].step(_target, cfg, c.dt_ms);

    for (uint8_t i = 0; i < WINDOWS; i++) {
        opn[i].set(act[i].open_cmd());
        clo[i].set(act[i].close_cmd());
        // Parked is every window AT THE TARGET, not every window commanded to
        // it: one at a time means the last of the four is a minute behind the
        // first, and a supervisor that dropped control at the request would
        // leave three of them wherever they were.
        if (inhibit_requested() &&
            (!act[i].known() || act[i].moving() ||
             fabsf(_target - act[i].position()) > cfg.deadband))
            parking();
    }
}

void WindowsController::observe(ProgramContext&) {
    // One target for all four - they are one roof - against each ram's own
    // position estimate, which is what says how far behind the sequence is.
    for (uint8_t i = 0; i < WINDOWS; i++) {
        tgt[i].set(_target);
        if (act[i].known()) pos[i].set(act[i].position());
        else pos[i].invalidate();
    }
}

}  // namespace Site
