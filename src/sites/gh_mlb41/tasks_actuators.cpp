/**
 * @file tasks_actuators.cpp
 * @brief Arbitration: the programs holding the plant's actuators, where the
 *        ladders' demands become one position per device.
 *
 * The interfaces are in tasks.h; what each law does is here, so the header
 * reads as the list of programs and their variables.
 */

#include <cmath>

#include "tasks_actuators.h"

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

void ScreenController::_command(bool deploy) {
    // A CHANGE starts the traverse; observe() times it. Both ways, because the
    // fabric is in the fans' way whichever direction it travels.
    _command_changed = !_seeded || (deploy != _deployed);
    _deployed = deploy;
    _seeded   = true;
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
        // through the roof all night, and the fabric is what stops it. Free,
        // too: the coil is de-energised there.
        _command(true);
        if (_command_changed || _travelling.q()) parking();
        return;
    }

    // An unknown cooling demand DEPLOYS it: that is this one's safe state, and
    // it should never be reached - the interior is measured by three probes at
    // each of two levels precisely so that it is not.
    const bool cool_ok = _v.cool_dem.valid();
    if (!cool_ok) degraded();

    // No dead band on this one, and none is needed: the permission table above
    // makes every phase ONE-DIRECTIONAL, so a demand sitting on the rung can
    // move the fabric at most once per phase whatever it does afterwards.
    const bool cold = !cool_ok ||
                      _v.cool_dem.get() <= _v.deploy_dem.get();
    // The phase gives PERMISSION, the demand asks for the move - and nothing
    // else does. A phase commanding an actuator is the one thing the design
    // rules out: a phase sets the target and enables a ladder, and the ladder
    // decides what moves. A cold night reaches the fabric by driving the
    // cooling demand under cli_thermalScreenDem like any other cold hour.
    const DayPhase phase = (DayPhase)_v.phase.get();
    if (_allows(phase, cold)) _command(cold);
    // A command that has just CHANGED is a traverse about to start, and
    // observe() has not timed it yet: parked is neither.
    if (_command_changed || _travelling.q()) parking();
}

void ScreenController::observe(ProgramContext& c) {
    // Timed HERE, so a screen parked on the way to its safe position still
    // reports itself as moving and the fans stay cut.
    //
    // The pulse is capped at the actuator's own SAFE time, which is the
    // datasheet's number where the travel is the operator's: this traverse is
    // what the fans are cut for and what a parking supervisor waits on, so a
    // mis-set travel would hold both for as long as it said.
    const uint32_t travel = 1000u * (uint32_t)_v.travel_s.get();
    const uint32_t safe   = 1000u * (uint32_t)_v.travel_tmo_s.get();

    _v.moving.set(_travelling(_moved(_command_changed),
                              (travel < safe) ? travel : safe, c.dt_ms));
    _command_changed = false;
}

// WindowsController

const float WindowsController::SAFE_OPEN = 0.25f;

Plc::ActuatorConfig WindowsController::_mechanics() const {
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
    // READ BACK, never the local verdicts: each row is what an operator can
    // force, and a lockout the law then ignored would be a lie on the page.
    // Forcing one is also the only way to test the roof's stage-1 arbitration
    // without waiting for weather.
    _v.window_lock.set(_v.rain_lock.get() || _v.wind_lock.get() ||
                       _v.supply_lock.get());
    return _v.window_lock.get();
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

    const Plc::ActuatorConfig cfg = _mechanics();

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

void WindowsController::observe(ProgramContext&) {
    // One target for all four - they are one roof - against each ram's own
    // position estimate, which is what says how far behind the sequence is.
    for (uint8_t i = 0; i < WINDOWS; i++) {
        _v.tgt[i].set(_target);
        if (_act[i].known()) _v.pos[i].set(_act[i].position());
        else _v.pos[i].invalidate();
    }
}

// IntractorController

float IntractorController::_safe_speed() const {
    // The AO row's declared safe value, not a constant here: an actuator's safe
    // state belongs to the table, which is also what the field bus carries out
    // when control authority goes entirely.
    const Vars::Descriptor& d = _v.intr[0].io->descriptor(_v.intr[0].r.index);
    return (float)Vars::as_f32(d.type, d.def);
}

void IntractorController::run(ProgramContext& c) {
    (void)c;
    // The FLOOR, and it is a SPEED rather than a demand: some air is always
    // wanted, and the row is where this actuator's safe state is declared. A
    // demand below it simply does not reach it.
    float speed = _safe_speed();

    if (!inhibit_requested()) {
        const bool  cool_ok = _v.cool_dem.valid();
        const float cool    = _v.cool_dem.get();
        if (!cool_ok) degraded();

        // Three demands, each gated by the law that owns the reading behind it,
        // and a demand nobody can read contributes NOTHING - it does not have
        // to, since the floor below is already this actuator's safe state.
        //
        //  - cooling, while the intake really is cooler
        //  - the anti-rot ventilation, ungated: rot is the collection, and the
        //    dew point it fights is the one inside
        //  - the DEHUMIDIFY arm alone, while the air outside is drier. Positive
        //    is asking for the skirt, and more air would work against it
        const float cooling = (cool_ok && _v.cool_ok.get()) ? cool : 0.0f;
        const float venting = _v.rot_vent.get();
        const float drying  = _v.dry_ok.get() ? -_v.humid_dem.get() : 0.0f;
        // ...and the OTHER arm too. Too little humidity is answered by the
        // skirt and these fans TOGETHER: the air they draw comes in through the
        // wetted textile, which is the only humidifier the plant has. Ungated -
        // the point is to bring moisture in, so "is it drier outside" is the
        // wrong question for this arm.
        const float wetting = Plc::max_of(0.0f, _v.humid_dem.get());

        const float demand = Plc::max_of(Plc::max_of(cooling, venting),
                                         Plc::max_of(drying, wetting));
        speed = Plc::max_of(speed, Plc::ladder(demand, _v.start_dem.get(),
                                               _v.full_dem.get()));

        // Ground heat is ONE RUNG, not a ramp: the spec's negative half asks
        // for low speed at -0.1 and says nothing about wanting more of it the
        // colder it gets. Pulling harder on a cold house moves more air through
        // the same ground, cooling the ground rather than warming the house.
        if (_v.ground_ok.get())
            speed = Plc::max_of(speed, _v.ground_speed.get());
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

float FanController::_demand() const {
    // An unknown demand is NOT "nothing to do". Either ladder invalidates its
    // own when it cannot see what it is about, and still air is itself a rot
    // risk - so an unknown one contributes these fans' OWN lowest band, which
    // is their safe state, rather than zero.
    const float floor    = _v.low_dem.get();
    const float strat    = _v.strat_dem.valid() ? _v.strat_dem.get() : floor;
    const float cooling  = _v.cool_dem.valid() ? _v.cool_dem.get() : floor;
    // A cycle in progress wants air moving over the trays it has just wetted.
    const bool  cycling  = _v.irr_state.get() != IRR_IDLE;
    const float watering = cycling ? FAN_IRR_DEM : 0.0f;

    // The day's two breezes. At dusk it dries the leaves before the stomata
    // open - these plants breathe at night, and a wet leaf cannot - and at dawn
    // it takes the night's condensation off them. A FLOOR like the others: a
    // cooling demand above it still takes the fans higher.
    const DayPhase phase = (DayPhase)_v.phase.get();
    const bool breezing  = (phase == PHASE_DAWN_BREEZE ||
                            phase == PHASE_DUSK_BREEZE);
    const float breeze   = breezing ? _v.breeze_dem.get() : 0.0f;

    return Plc::max_of(Plc::max_of(strat, cooling),
                       Plc::max_of(watering, breeze));
}

uint8_t FanController::_band(float demand) {
    // Three rungs, each engaging at its own threshold and released a little
    // under it, which is what keeps a demand sitting on an edge from stepping
    // the relays. The block is the same one the destratification ladder uses.
    const float step[3] = { _v.low_dem.get(), _v.med_dem.get(),
                            _v.high_dem.get() };
    return _steps(demand, step, FAN_DEM_HYST);
}

void FanController::_drive(uint8_t speed) {
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
        _drive(0);
        return;
    }
    if (inhibit_requested()) {
        // Speed 1, not off, and the table's safe column says the same: still
        // air is itself a rot risk, so the fans are one of the few things that
        // keep working once everything else has stopped.
        _speed = 1;
        _steps.preset(1);
        _drive(1);
        return;
    }
    if (!_v.strat_dem.valid() ||
        !_v.cool_dem.valid()) degraded();
    _speed = _band(_demand());
    _drive(_speed);
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
