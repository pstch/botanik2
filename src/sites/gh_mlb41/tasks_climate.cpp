/**
 * @file tasks_climate.cpp
 * @brief The climate ladders: the day's phase, the target it sets, and the
 *        cooling, destratification and humidity demands.
 *
 * The interfaces are in tasks.h; what each law does is here, so the header
 * reads as the list of programs and their variables.
 */


#include "tasks.h"

namespace Site {

using namespace Botanik;

const float TargetTempController::TARGET_MIN = 14.0f;
const float TargetTempController::TARGET_MAX = 27.0f;

// DayPhaseController

int16_t DayPhaseController::_minutes(int64_t v) {
    if (v < -1440) return -1440;
    if (v >  1440) return  1440;
    return (int16_t)v;
}

bool DayPhaseController::_solar_day(int16_t& from_rise, int16_t& from_set) const {
    if (!_sun.ready()) return false;
    const uint64_t now = HAL::Clock::wall_ms();
    // Signed, and in the caller's units: both events belong to the same solar
    // day, so one of the two is always in the past and the other in the future.
    const int64_t rise = ((int64_t)now - (int64_t)_sun.sunrise_ms()) / 60000;
    const int64_t set  = ((int64_t)now - (int64_t)_sun.sunset_ms())  / 60000;
    // Clamped to a day either side, in whole minutes: an int16 row, and a
    // clock that has jumped is not a reason to publish a nonsense offset.
    from_rise = _minutes(rise);
    from_set  = _minutes(set);
    return true;
}

DayPhase DayPhaseController::_phase_at(int16_t from_rise,
                                       int16_t from_set) const {
    // Down the day in order, so a boundary an operator has trimmed past the one
    // after it shortens its own phase rather than swallowing the next.
    if (from_rise < _v.cold_bank_rise.get()) return PHASE_NIGHT;
    if (from_rise < _v.breeze_rise.get())
        return _v.cold_arm.get() ? PHASE_COLD_BANK : PHASE_NIGHT;
    if (from_rise < _v.day_rise.get())       return PHASE_DAWN_BREEZE;
    if (from_set  < _v.heat_bank_set.get())  return PHASE_DAY;
    if (from_set  < _v.dusk_set.get())
        return _v.heat_arm.get() ? PHASE_HEAT_BANK : PHASE_DAY;
    if (from_set  < _v.night_set.get())      return PHASE_DUSK_BREEZE;
    return PHASE_NIGHT;
}

void DayPhaseController::run(ProgramContext& c) {
    const float inside = _v.inside.get();

    if (c.first_scan || !_seeded) {
        _day_max = _night_min = inside;
        _seeded  = true;
    }
    if (inside > _day_max)   _day_max = inside;
    if (inside < _night_min) _night_min = inside;

    // The extremes are gathered per phase, not per calendar day: what COLD_BANK
    // needs is how hot it got before it, and the phase boundary is where that
    // window closes.
    const DayPhase was = _phase;
    int16_t from_rise = 0, from_set = 0;
    if (_solar_day(from_rise, from_set)) {
        _phase = _phase_at(from_rise, from_set);
        _v.from_rise.set(from_rise);
        _v.from_set.set(from_set);
    } else {
        // No location, or no wall clock yet: hold the phase rather than
        // announcing a night the plant is not in. Every boot passes through
        // here, for as long as NTP takes. The offsets ARE the sun, so they say
        // they are not known rather than holding yesterday's numbers.
        _v.from_rise.invalidate();
        _v.from_set.invalidate();
        degraded();
    }
    if (_phase == PHASE_DAY && was != PHASE_DAY)     _day_max   = inside;
    if (_phase == PHASE_NIGHT && was != PHASE_NIGHT) _night_min = inside;

    _v.phase.set((uint8_t)_phase);
    _v.prev_day_max.set(_day_max);
    _v.prev_night_min.set(_night_min);

    // Whether the bank phases have anything to do when their turn comes. Both
    // can be armed at once - a clear late-spring day is hot and its night cold,
    // and the two are decided independently.
    _v.cold_arm.set(_day_max > _v.cold_bank_trg.get());
    _v.heat_arm.set(_night_min < _v.heat_bank_trg.get());
}

// TargetTempController

void TargetTempController::run(ProgramContext& c) {
    const DayPhase phase = (DayPhase)_v.phase.get();
    float target = _v.cool_set.get();

    // A bank phase shifts the base only when yesterday justified it: pre-cool
    // before dawn after a hot day, bank heat at dusk after a cold night. In
    // force but not armed, the phase runs at the ordinary target.
    if (phase == PHASE_COLD_BANK && _v.cold_arm.get())
        target -= _v.cold_bank_ofs.get();
    else if (phase == PHASE_HEAT_BANK && _v.heat_arm.get())
        target += _v.heat_bank_ofs.get();

    // The clamp is what keeps an aggressively-set offset from pushing the
    // greenhouse somewhere dangerous, whatever the phase asked for.
    _v.target.set(Plc::limit(target, TARGET_MIN, TARGET_MAX));
}

// CoolingController

void CoolingController::run(ProgramContext& c) {
    if (c.first_scan) _pid.reset();   // the integral is a claim about the past

    const bool hi_ok = _v.ceiling.valid();
    const bool lo_ok = _v.floor.valid();
    if (!hi_ok && !lo_ok) {
        // No interior temperature at all. A PID handed a defaulted reading
        // would see 0 degC against the target and call for full HEAT - into a
        // greenhouse nobody can see. Say the demand is unknown instead, and let
        // each actuator go to its own safe state. The window demand goes with
        // it: an opening derived from a temperature nobody has is not an
        // intention, and the windows have their own safe position.
        _v.demand.invalidate();
        _v.window.invalidate();
        _pid.reset();      // the integral is about a plant we stopped seeing
        faulted();
        return;
    }
    // The ceiling by preference; the floor is a worse place to control from and
    // a better one than nowhere.
    const float inside = hi_ok ? _v.ceiling.get() : _v.floor.get();
    if (!hi_ok) degraded();

    // The parameters are in the TIME form an operator tunes in (Ti, Td in
    // seconds); PidTuning takes gains. Ti of 0 means no integral action rather
    // than an infinite one, which is what a divide would give.
    const float kp = _v.kp.get();
    const float ti = _v.ti.get();
    const float td = _v.td.get();
    const Plc::PidTuning t(kp,
                           (ti > 0.0f) ? (kp / ti) : 0.0f,
                           kp * td,
                           -1.0f, 1.0f,
                           /* d_tau_s */ 0.0f,
                           /* reverse */ true);

    // Reverse-acting: the demand RISES as the reading does, so above the target
    // it is positive (cooling) and below it negative (the intractors drawing
    // warm floor air, and lower still the thermal screen).
    const float demand = _pid(_v.target.get(), inside, t, c.dt_ms);
    _v.demand.set(demand);
    // Where the ladder wants the windows, published as the one INTENDED
    // opening: the windows apply the interlocks to it, and an operator can see
    // what was asked for beside where each ram actually is.
    _v.window.set(Plc::ladder(demand, _v.win_start.get(),
                                        _v.win_full.get()));
    _intake_gates(c, inside, demand);
}

void CoolingController::_intake_gates(ProgramContext& c, float inside,
                                      float demand) {
    // Drawing air only moves the interior toward whatever the intake is. Both
    // gates are shut while the crawlspace probe is out, which costs a little
    // ventilation and cannot cost a plant: the windows and the fans are on
    // their own paths.
    const bool intake_ok = _v.intake.valid();
    if (!intake_ok) degraded();
    const float intake = _v.intake.get();
    const float margin = _v.temp_margin.get();

    _v.intake_ok.set(intake_ok && (inside - intake) >= margin);
    // The mirror, and the only place the negative half of the ladder does
    // anything: cold enough to be asking for heat, and ground warm enough to
    // be worth pulling it out of.
    _v.ground_ok.set(intake_ok &&
                               demand <= _v.ground_dem.get() &&
                               (intake - inside) >= margin);
}

// DestratController

void DestratController::run(ProgramContext& c) {
    if (c.first_scan) _bands.reset();

    // A band chosen from a gap nobody measured is a speed chosen from a
    // DEFAULT, and this row's default is 0 - which reads as "no stratification"
    // and stops the fans. Say the demand is unknown instead and let each
    // actuator decide what that means for it.
    if (!_v.strat.valid()) {
        _v.demand.invalidate();
        // The bands are a claim about where the gap has recently been, and we
        // stopped seeing it.
        _bands.reset();
        faulted();
        return;
    }

    // Three fan speeds, so three rungs on one staircase, each engaging at its
    // own gap and released by the common hysteresis. All three, not two with
    // the middle one decided by a bare comparison: a speed change needs the gap
    // to have really moved, whichever pair of speeds it is between.
    const float edge[3] = { _v.low.get(), _v.med.get(),
                            _v.high.get() };
    const uint8_t speed = _bands(_v.strat.get(), edge, _v.hyst.get());

    // The demand the arbitration reads is the FANS' OWN rung for that speed.
    // Any other number would have to be translated twice, and the two ladders
    // would drift apart the first time one of them was retuned.
    const float rung[4] = { 0.0f, _v.fan_low.get(),
                            _v.fan_med.get(), _v.fan_high.get() };
    _v.demand.set(rung[speed]);
}

// HumidityController

/** @brief How long the margin must stay above the limit before humidifying is
 *  allowed again. In TIME, because the limit it releases against is already a
 *  parameter: what this adds is that a margin hovering on that limit does not
 *  let the skirt back on every other scan. */
static const uint32_t ROT_RELEASE_MS = 300000;

void HumidityController::observe(ProgramContext& c) {
    if (!_v.dp_margin.valid()) {
        // No margin, no verdict on it - and the guard fails toward acting:
        // ventilating costs energy, not ventilating costs the collection.
        degraded();
        _v.vent_dem.set(1.0f);
        _v.lockout.set(_lockout_for(true, ROT_RELEASE_MS, c.dt_ms));
        return;
    }

    const float margin = _v.dp_margin.get();
    const float limit  = Plc::limit(_v.rot_limit.get(), ROT_MARGIN_DEGC, 10.0f);

    // A FALLING ramp, negated so the one ladder function serves both
    // directions: nothing at the limit, everything at the dew point itself.
    // The span IS the operator's setting - cli_tooHumid_degC sits on its own
    // floor by default, so a ramp to that floor would be a step.
    _v.vent_dem.set(Plc::ladder(-margin, -limit, 0.0f));

    // An OVERRUN, not a dwell: the lockout follows the condition up at once and
    // outlives it, because humidifying into air that has only just dried out is
    // what puts the margin back where it was. Nothing is locked out at boot.
    _v.lockout.set(_lockout_for(_v.rot.get(), ROT_RELEASE_MS, c.dt_ms));
    _dry_gate(c);
}

void HumidityController::_dry_gate(ProgramContext& c) {
    // DEW POINTS, not humidities: air at 90% and 5 degC is drier than air at
    // 60% and 25, and swapping one for the other is what ventilating does.
    const bool have = _v.in_t.valid() &&
                      _v.in_rh.valid() &&
                      _v.ext_t.valid() &&
                      _v.ext_rh.valid();
    if (!have) {
        // Shut, and said: opening on a guess would mean drawing in air that may
        // be wetter than what is already here, which is the one thing this
        // gate exists to prevent. The rot ladder is not gated and still vents.
        degraded();
        _v.dry_ok.set(false);
        return;
    }
    const float inside  = Plc::dew_point(_v.in_t.get(), _v.in_rh.get());
    const float outside = Plc::dew_point(_v.ext_t.get(), _v.ext_rh.get());
    _v.dry_ok.set((inside - outside) >= _v.dry_margin.get());
}

void HumidityController::run(ProgramContext& c) {
    (void)c;
    if (!_v.dp_margin.valid()) {
        // The ladder is a function of the margin, and its default is a number
        // that means "saturated": published as a demand it would call for full
        // dehumidification on a reading nobody has. The rot guard in observe()
        // still ventilates - that one is a CHOICE, not a derivation.
        _v.demand.invalidate();
        return;
    }

    // Four bands and a dead zone, and the signed ladder IS that shape: below
    // dehumStart the margin is closing on condensation (negative, dehumidify);
    // above humidStart the air is drier than the plants want (positive); in
    // between nothing moves.
    _v.demand.set(Plc::signed_ladder(_v.dp_margin.get(),
                                     _v.dehum_full.get(),
                                     _v.dehum_start.get(),
                                     _v.humid_start.get(),
                                     _v.humid_full.get()));
}

}  // namespace Site
