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

bool ScreenController::_allows(DayPhase phase, bool deploy) {
    // Deploying belongs to the half of the day that is closing down, retracting
    // to the half that is opening up. An hour of cold at midday therefore never
    // shuts the fabric against the sun, and a mild night never opens it and
    // gives back the heat it was holding.
    return deploy ? (phase == PHASE_NIGHT || phase == PHASE_DUSK_BREEZE ||
                     phase == PHASE_HEAT_BANK)
                  : (phase == PHASE_DAY || phase == PHASE_DAWN_BREEZE ||
                     phase == PHASE_COLD_BANK);
}

void ScreenController::_command(Vars::Bank& io, bool deploy) {
    if (_seeded && deploy == _deployed) return;
    // Both ways: the fabric is in the fans' way whichever direction it travels.
    // Capped at the actuator's own safe time, which is the DATASHEET's number
    // where the travel is the operator's: this traverse is what the fans are
    // cut for and what a parking supervisor waits on, and a mis-set travel
    // would hold both for as long as it says.
    const uint32_t want = 1000u * (uint32_t)_v.travel_s.get();
    const uint32_t cap  = 1000u * (uint32_t)_v.travel_tmo_s.get();
    _travel_ms = (want < cap) ? want : cap;
    _deployed  = deploy;
    _seeded    = true;
    _v.screen.set(deploy);
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
    const bool cool_ok = _v.cool_dem.valid();
    if (!cool_ok) degraded();
    const bool cold = !cool_ok ||
                      _v.cool_dem.get() <= _v.deploy_dem.get();
    // The night rule is a floor under the demand one: the ladder is written
    // about the day, and a clear night loses heat with no demand to show it.
    const DayPhase phase = (DayPhase)_v.phase.get();
    const bool night = (phase == PHASE_NIGHT) &&
                       (_v.inside.get() < _v.night_degC.get());

    // The phase gives PERMISSION, the demand asks for the move. A move the
    // phase forbids leaves the fabric where it is, and the thermal need goes to
    // the intractors pulling ground heat instead.
    const bool want = cold || night;
    if (_allows(phase, want)) _command(c.bank, want);
    if (_travel_ms > 0) parking();
}

void ScreenController::observe(ProgramContext& c) {
    // Counted HERE, so a screen parked on the way to its safe position still
    // reports itself as moving and the fans stay cut.
    _travel_ms = (_travel_ms > c.dt_ms) ? (_travel_ms - c.dt_ms) : 0;
    _v.moving.set(_travel_ms > 0);
}

// WindowsController

const float WindowsController::SAFE_OPEN = 0.25f;

Plc::ActuatorConfig WindowsController::_mechanics(const Vars::Bank& io) const {
    const uint32_t travel = 1000u * _v.travel_s.get();
    const uint32_t tmo    = 1000u * _v.tmo_s.get();
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
    const bool blind = !_v.rain_mmph.valid() ||
                       !_v.wind_mps.valid();
    if (blind) degraded();

    // An OVERRUN on the RAIN: the roof stays shut for the declared time after
    // the last drop, because a shower that has just stopped is about to be one
    // that has not. Being BLIND is not a shower - it is a live interlock that
    // holds while it lasts and lets go the moment the station answers, where
    // feeding it the overrun cost the plant a quarter of an hour of ventilation
    // after every restart.
    const bool rain_lock =
        _rain_for(_v.rain_mmph.get() > 0.0f, 1000u * _v.rain_hold_s.get(),
                  c.dt_ms) || blind;

    // The GUST, not the average: what tears a window off its ram is the peak,
    // and the station's own window is the hysteresis - no dwell needed here.
    const float gust = _v.gust_mps.get();
    const float mean = _v.wind_mps.get();
    const bool wind_lock =
        blind || ((gust > mean ? gust : mean) >= _v.wind_shut.get());

    _v.rain_lock.set(rain_lock);
    _v.wind_lock.set(wind_lock);
    return rain_lock || wind_lock || _v.supply_lock.get();
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
    } else if (!_v.window_dem.valid()) {
        degraded();
        _target = SAFE_OPEN;
    } else {
        _target = Plc::limit(_v.window_dem.get(), 0.0f, 1.0f);
    }

    const int8_t owner = _owner(_target, cfg);
    if (owner >= 0) _act[owner].step(_target, cfg, c.dt_ms);

    for (uint8_t i = 0; i < WINDOWS; i++) {
        _v.opn[i].set(_act[i].open_cmd());
        _v.clo[i].set(_act[i].close_cmd());
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
        _v.tgt[i].set(_target);
        if (_act[i].known()) _v.pos[i].set(_act[i].position());
        else _v.pos[i].invalidate();
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
        const float dehum = _v.dry_ok.get() ? -_v.humid_dem.get()
                                                  : 0.0f;
        // A demand nobody can read contributes NOTHING to the maximum. It does
        // not have to: the floor below is already this actuator's safe state,
        // and the other two can still call for more on their own account.
        const bool cool_ok = _v.cool_dem.valid();
        if (!cool_ok) degraded();
        const float cool = _v.cool_dem.get();
        // Cooling only counts while the intake is actually cooler; the mirror
        // gate turns the NEGATIVE half into a demand of its own, which is the
        // one thing that pulls stored ground heat up into the house.
        float worst = (cool_ok && _v.cool_ok.get()) ? cool : 0.0f;
        // Ungated, and deliberately: rot is the collection, and the dew point
        // it is fighting is the one inside.
        if (_v.rot_vent.get() > worst) worst = _v.rot_vent.get();
        if (dehum > worst) worst = dehum;

        const float want = Plc::ladder(worst, _v.start_dem.get(),
                                       _v.full_dem.get());
        if (want > speed) speed = want;

        // Ground heat is ONE RUNG, not a ramp: the spec's negative half asks
        // for low speed at -0.1 and says nothing about wanting more of it the
        // colder it gets. Pulling harder on a cold house moves more air through
        // the same ground, which cools the ground rather than warming the
        // house. A floor like the others.
        const float ground = _v.ground_speed.get();
        if (_v.ground_ok.get() && ground > speed) speed = ground;
    }

    for (uint8_t i = 0; i < INTRACTORS; i++) _v.intr[i].set(speed);
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
    float worst = _v.strat_dem.valid() ? _v.strat_dem.get()
                                                 : _v.low_dem.get();
    const float cool = _v.cool_dem.valid() ? _v.cool_dem.get()
                                                     : _v.low_dem.get();
    if (cool > worst) worst = cool;
    if (_v.irr_state.get() != IRR_IDLE && FAN_IRR_DEM > worst)
        worst = FAN_IRR_DEM;
    return worst;
}

uint8_t FanController::_band(const Vars::Bank& io, float demand) {
    // Three rungs, each engaging at its own threshold and released a little
    // under it, which is what keeps a demand sitting on an edge from stepping
    // the relays. The block is the same one the destratification ladder uses.
    const float step[3] = { _v.low_dem.get(), _v.med_dem.get(),
                            _v.high_dem.get() };
    return _steps(demand, step, FAN_DEM_HYST);
}

void FanController::_drive(Vars::Bank& io, uint8_t speed) {
    (void)io;
    for (uint8_t i = 0; i < FANS; i++) {
        _v.speed1[i].set(speed == 1);
        _v.speed2[i].set(speed == 2);
        _v.speed3[i].set(speed == 3);
    }
    _v.band.set(speed);
}

void FanController::run(ProgramContext& c) {
    // MECHANICAL, so it is resolved before any demand and before the safe
    // state: the fabric is travelling through the air these blow into.
    if (c.first_scan) _steps.reset();
    if (_v.screen_moving.get()) {
        _speed = 0;
        _steps.reset();
        _drive(c.bank, 0);
        return;
    }
    if (inhibit_requested()) {
        // Speed 1, not off, and the table's safe column says the same: still
        // air is itself a rot risk, so the fans are one of the few things that
        // keep working once everything else has stopped.
        _speed = 1;
        _steps.preset(1);
        _drive(c.bank, 1);
        return;
    }
    if (!_v.strat_dem.valid() ||
        !_v.cool_dem.valid()) degraded();
    _speed = _band(c.bank, _demand(c.bank));
    _drive(c.bank, _speed);
}

// SkirtController

void SkirtController::bind(Vars::Bank& io) {
    _v.bind(io, *this);
    _valve.attach(_v.valve);
}

void SkirtController::run(ProgramContext& c) {
    // Wetting air that is already condensing is the one thing this valve must
    // never do, so the lockout is resolved before any demand - and before the
    // pulse phase, which starts again from shut.
    if (inhibit_requested() || _v.rot_lock.get()) {
        _valve.park();
        _valve.reset();
        _v.duty.set(0.0f);
        return;
    }

    // PROPORTIONAL on the humidify arm: the skirt IS the humidifier, and the
    // ladder's effort is the on-time. The dehumidify arm is the intractors'.
    const float humid = Plc::limit(_v.humid_dem.get(), 0.0f, 1.0f);

    // STAGED on the cooling axis, and gated on the air being dry enough for a
    // wet pad to cool at all - in damp air it only adds water. An unknown
    // demand contributes this valve's own safe state, which is SHUT.
    if (!_v.cool_dem.valid()) degraded();
    const float cool =
        (_v.cool_dem.valid() &&
         _v.dp_margin.get() >= _v.dry_min.get())
            ? Plc::ladder(_v.cool_dem.get(), _v.cool_start.get(), 1.0f)
            : 0.0f;

    const float duty = _valve.modulate((humid > cool) ? humid : cool,
                                       1000u * _v.period_s.get(),
                                       1000u * _v.min_on_s.get(),
                                       c.dt_ms);
    _v.duty.set(duty);
}

}  // namespace Site
