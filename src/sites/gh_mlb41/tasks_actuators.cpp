/**
 * @file tasks_actuators.cpp
 * @brief Arbitration: the programs holding the plant's actuators, where the
 *        ladders' demands become one position per device.
 *
 * The interfaces are in tasks.h; what each law does is here, so the header
 * reads as the list of programs and their variables.
 */

#include <cmath>

#include "tasks.h"

namespace Site {

using namespace Botanik;

// ScreenController

void ScreenController::_command(Vars::Bank& io, bool deploy) {
    if (_seeded && deploy == _deployed) return;
    // Both ways: the fabric is in the fans' way whichever direction it travels.
    // Capped at the actuator's own safe time, which is the DATASHEET's number
    // where the travel is the operator's: this traverse is what the fans are
    // cut for and what a parking supervisor waits on, and a mis-set travel
    // would hold both for as long as it says.
    const uint32_t want = 1000u * (uint32_t)io.get(_v.travel_s);
    const uint32_t cap  = 1000u * (uint32_t)io.get(_v.travel_tmo_s);
    _travel_ms = (want < cap) ? want : cap;
    _deployed  = deploy;
    _seeded    = true;
    io.drive(_v.screen, deploy);
}

void ScreenController::run(ProgramContext& c) {
    if (c.first_scan) {
        // Where the fabric actually is, nothing here knows. Command it afresh
        // and let the traverse run, rather than trusting a position from
        // before control was withdrawn.
        _seeded = false;
    }
    if (inhibit_requested()) {
        // Safe is DEPLOYED. A greenhouse nobody is controlling loses heat
        // through the roof all night, and the fabric is what stops it.
        _command(c.bank, true);
        if (_travel_ms > 0) parking();
        return;
    }

    // An unknown cooling demand DEPLOYS it: that is this one's safe state, and
    // it should never be reached - the interior is measured by three probes at
    // each of two levels precisely so that it is not.
    const bool cool_ok = c.bank.var(_v.cool_dem.r).valid();
    if (!cool_ok) degraded();
    const bool cold = !cool_ok ||
                      c.bank.get(_v.cool_dem) <= c.bank.get(_v.deploy_dem);
    // The night rule is a floor under the demand one: the ladder is written
    // about the day, and a clear night loses heat with no demand to show it.
    const bool night = ((DayPhase)c.bank.get(_v.phase) == PHASE_NIGHT) &&
                       (c.bank.get(_v.inside) < c.bank.get(_v.night_degC));
    _command(c.bank, cold || night);
    if (_travel_ms > 0) parking();
}

void ScreenController::observe(ProgramContext& c) {
    // Counted HERE, so a screen parked on the way to its safe position still
    // reports itself as moving and the fans stay cut.
    _travel_ms = (_travel_ms > c.dt_ms) ? (_travel_ms - c.dt_ms) : 0;
    c.bank.drive(_v.moving, _travel_ms > 0);
}

// WindowsController

const float WindowsController::SAFE_OPEN = 0.25f;

Plc::ActuatorConfig WindowsController::_mechanics(const Vars::Bank& io) const {
    const uint32_t travel = 1000u * io.get(_v.travel_s);
    const uint32_t tmo    = 1000u * io.get(_v.tmo_s);
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
    const bool blind = !c.bank.var(_v.rain_mmph.r).valid() ||
                       !c.bank.var(_v.wind_mps.r).valid();
    if (blind) degraded();

    const bool raining = blind || (c.bank.get(_v.rain_mmph) > 0.0f);
    // An OVERRUN on the rain: the roof stays shut for the declared time after
    // the last drop, because a shower that has just stopped is about to be one
    // that has not.
    const bool rain_lock =
        _rain_for(raining, 1000u * c.bank.get(_v.rain_hold_s), c.dt_ms);

    // The GUST, not the average: what tears a window off its ram is the peak,
    // and the station's own window is the hysteresis - no dwell needed here.
    const float gust = c.bank.get(_v.gust_mps);
    const float mean = c.bank.get(_v.wind_mps);
    const bool wind_lock =
        blind || ((gust > mean ? gust : mean) >= c.bank.get(_v.wind_shut));

    c.bank.drive(_v.rain_lock, rain_lock);
    c.bank.drive(_v.wind_lock, wind_lock);
    return rain_lock || wind_lock || c.bank.get(_v.supply_lock);
}

int8_t WindowsController::_owner(float target,
                                 const Plc::ActuatorConfig& cfg) const {
    // A stroke is committed: whoever is moving keeps the bus until it stops,
    // or the next selection would leave a coil energised with nothing stepping
    // it down.
    for (uint8_t i = 0; i < WINDOWS; i++)
        if (_act[i].moving()) return (int8_t)i;
    for (uint8_t i = 0; i < WINDOWS; i++) {
        if (!_act[i].known()) return (int8_t)i;
        if (fabsf(target - _act[i].position()) > cfg.deadband) return (int8_t)i;
    }
    return -1;
}

void WindowsController::run(ProgramContext& c) {
    if (c.first_scan) {
        // Where the rams actually are, nothing here knows: control was away
        // and the coils were dropped mid-stroke for all we can tell. Forget
        // the estimate and let the next command datum on a stop.
        for (uint8_t i = 0; i < WINDOWS; i++) _act[i].home();
        _rain_for.reset();
    }

    const Plc::ActuatorConfig cfg = _mechanics(c.bank);

    // ALWAYS, and before anything else: it publishes the two lockout rows, and
    // an observation does not stop being made because this law is parked.
    const bool shut_in = _shut_in(c);

    if (shut_in) {
        // Stage 1 of the arbitration, and absolute: rain and wind shut the roof
        // whatever else is true, including the safe state below.
        _target = 0.0f;
    } else if (inhibit_requested()) {
        _target = SAFE_OPEN;
    } else if (!c.bank.var(_v.cool_dem.r).valid()) {
        degraded();
        _target = SAFE_OPEN;
    } else {
        _target = Plc::ladder(c.bank.get(_v.cool_dem),
                              c.bank.get(_v.start_dem),
                              c.bank.get(_v.full_dem));
    }

    const int8_t owner = _owner(_target, cfg);
    if (owner >= 0) _act[owner].step(_target, cfg, c.dt_ms);

    for (uint8_t i = 0; i < WINDOWS; i++) {
        c.bank.drive(_v.opn[i], _act[i].open_cmd());
        c.bank.drive(_v.clo[i], _act[i].close_cmd());
        // Parked is every window AT THE TARGET, not every window commanded to
        // it: one at a time means the last of the four is a minute behind the
        // first, and a supervisor that dropped control at the request would
        // leave three of them wherever they were.
        if (inhibit_requested() &&
            (!_act[i].known() || _act[i].moving() ||
             fabsf(_target - _act[i].position()) > cfg.deadband))
            parking();
    }
}

void WindowsController::observe(ProgramContext& c) {
    // One target for all four - they are one roof - against each ram's own
    // position estimate, which is what says how far behind the sequence is.
    for (uint8_t i = 0; i < WINDOWS; i++) {
        c.bank.drive(_v.tgt[i], _target);
        if (_act[i].known()) c.bank.drive(_v.pos[i], _act[i].position());
        else c.bank.var(_v.pos[i].r).invalidate(Vars::Q_NO_DATA);
    }
}

// IntractorController

float IntractorController::_safe_speed(const Vars::Bank& io) const {
    // The AO row's declared safe value, not a constant here: an actuator's safe
    // state belongs to the table, which is also what the field bus carries out
    // when control authority goes entirely.
    const Vars::Descriptor& d = io.descriptor(_v.intr[0].r.index);
    return (float)Vars::as_f32(d.type, d.def);
}

void IntractorController::run(ProgramContext& c) {
    // The FLOOR, and it is a SPEED rather than a demand: some air is always
    // wanted, and the row is where this actuator's safe state is declared. A
    // demand below it simply does not reach it.
    float speed = _safe_speed(c.bank);

    if (!inhibit_requested()) {
        // Only the DEHUMIDIFY arm of the humidity demand: positive is asking
        // for the skirt, and drawing more air through would work against it.
        // Gated: air that is not drier than what is here would undo it.
        const float dehum = c.bank.get(_v.dry_ok) ? -c.bank.get(_v.humid_dem)
                                                  : 0.0f;
        // A demand nobody can read contributes NOTHING to the maximum. It does
        // not have to: the floor below is already this actuator's safe state,
        // and the other two can still call for more on their own account.
        const bool cool_ok = c.bank.var(_v.cool_dem.r).valid();
        if (!cool_ok) degraded();
        const float cool = c.bank.get(_v.cool_dem);
        // Cooling only counts while the intake is actually cooler; the mirror
        // gate turns the NEGATIVE half into a demand of its own, which is the
        // one thing that pulls stored ground heat up into the house.
        float worst = (cool_ok && c.bank.get(_v.cool_ok)) ? cool : 0.0f;
        if (c.bank.get(_v.ground_ok) && -cool > worst) worst = -cool;
        // Ungated, and deliberately: rot is the collection, and the dew point
        // it is fighting is the one inside.
        if (c.bank.get(_v.rot_vent) > worst) worst = c.bank.get(_v.rot_vent);
        if (dehum > worst) worst = dehum;

        const float want = Plc::ladder(worst, c.bank.get(_v.start_dem),
                                       c.bank.get(_v.full_dem));
        if (want > speed) speed = want;
    }

    for (uint8_t i = 0; i < INTRACTORS; i++) c.bank.drive(_v.intr[i], speed);
}

// FanController

/** @brief Demand change needed to leave the speed band in force. On the fused
 *  DEMAND, not on any one signal: which input wins changes as the plant does,
 *  and the fans would step every time it did. */
static const float FAN_DEM_HYST = 0.05f;

/** @brief What a watering cycle asks of the fans. Fixed here rather than a
 *  parameter: it is the LOWEST band, the floor that keeps air over wet trays,
 *  and the bands themselves are already tunable. */
static const float FAN_IRR_DEM = 0.34f;

float FanController::_demand(const Vars::Bank& io) const {
    // An unknown gap is NOT "no stratification". The destratification ladder
    // invalidates its demand when it cannot see the profile, and still air is
    // itself a rot risk - so an unknown one contributes these fans' OWN lowest
    // band, which is their safe state, rather than nothing. A floor, not a
    // target: a cooling demand above it still takes them higher.
    float worst = io.var(_v.strat_dem.r).valid() ? io.get(_v.strat_dem)
                                                 : io.get(_v.low_dem);
    const float cool = io.var(_v.cool_dem.r).valid() ? io.get(_v.cool_dem)
                                                     : io.get(_v.low_dem);
    if (cool > worst) worst = cool;
    if (io.get(_v.irr_state) != IRR_IDLE && FAN_IRR_DEM > worst)
        worst = FAN_IRR_DEM;
    return worst;
}

uint8_t FanController::_band(const Vars::Bank& io, float demand) const {
    const float step[3] = { io.get(_v.low_dem), io.get(_v.med_dem),
                            io.get(_v.high_dem) };
    uint8_t up = 0;
    for (uint8_t i = 0; i < 3; i++)
        if (demand >= step[i]) up = (uint8_t)(i + 1);
    if (up > _speed) return up;
    // Coming DOWN costs the hysteresis: the threshold that let this speed in is
    // the one that has to be given back.
    if (_speed > 0 && demand < step[_speed - 1] - FAN_DEM_HYST) return up;
    return _speed;
}

void FanController::_drive(Vars::Bank& io, uint8_t speed) {
    for (uint8_t i = 0; i < FANS; i++) {
        io.drive(_v.speed1[i], speed == 1);
        io.drive(_v.speed2[i], speed == 2);
        io.drive(_v.speed3[i], speed == 3);
    }
}

void FanController::run(ProgramContext& c) {
    // MECHANICAL, so it is resolved before any demand and before the safe
    // state: the fabric is travelling through the air these blow into.
    if (c.bank.get(_v.screen_moving)) {
        _speed = 0;
        _drive(c.bank, 0);
        return;
    }
    if (inhibit_requested()) {
        // Speed 1, not off, and the table's safe column says the same: still
        // air is itself a rot risk, so the fans are one of the few things that
        // keep working once everything else has stopped.
        _speed = 1;
        _drive(c.bank, 1);
        return;
    }
    if (!c.bank.var(_v.strat_dem.r).valid() ||
        !c.bank.var(_v.cool_dem.r).valid()) degraded();
    _speed = _band(c.bank, _demand(c.bank));
    _drive(c.bank, _speed);
}

// SkirtController

void SkirtController::bind(Vars::Bank& io) {
    _v.bind(io, *this);
    _valve.attach(io, _v.valve);
}

void SkirtController::run(ProgramContext& c) {
    // Wetting air that is already condensing is the one thing this valve must
    // never do, so the lockout is resolved before any demand - and before the
    // pulse phase, which starts again from shut.
    if (inhibit_requested() || c.bank.get(_v.rot_lock)) {
        _valve.park(c.bank);
        _valve.reset();
        c.bank.drive(_v.duty, 0.0f);
        return;
    }

    // PROPORTIONAL on the humidify arm: the skirt IS the humidifier, and the
    // ladder's effort is the on-time. The dehumidify arm is the intractors'.
    const float humid = Plc::limit(c.bank.get(_v.humid_dem), 0.0f, 1.0f);

    // STAGED on the cooling axis, and gated on the air being dry enough for a
    // wet pad to cool at all - in damp air it only adds water. An unknown
    // demand contributes this valve's own safe state, which is SHUT.
    if (!c.bank.var(_v.cool_dem.r).valid()) degraded();
    const float cool =
        (c.bank.var(_v.cool_dem.r).valid() &&
         c.bank.get(_v.dp_margin) >= c.bank.get(_v.dry_min))
            ? Plc::ladder(c.bank.get(_v.cool_dem), c.bank.get(_v.cool_start), 1.0f)
            : 0.0f;

    const float duty = _valve.modulate(c.bank, (humid > cool) ? humid : cool,
                                       1000u * c.bank.get(_v.period_s),
                                       1000u * c.bank.get(_v.min_on_s),
                                       c.dt_ms);
    c.bank.drive(_v.duty, duty);
}

}  // namespace Site
