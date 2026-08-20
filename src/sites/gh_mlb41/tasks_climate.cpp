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

bool DayPhaseController::_solar_day(int16_t& from_rise, int16_t& from_set) const {
    if (!_sun.ready()) return false;
    const uint64_t now = HAL::Clock::wall_ms();
    // Signed, and in the caller's units: both events belong to the same solar
    // day, so one of the two is always in the past and the other in the future.
    const int64_t rise = ((int64_t)now - (int64_t)_sun.sunrise_ms()) / 60000;
    const int64_t set  = ((int64_t)now - (int64_t)_sun.sunset_ms())  / 60000;
    from_rise = (int16_t)Plc::limit(rise, (int64_t)-1440, (int64_t)1440);
    from_set  = (int16_t)Plc::limit(set,  (int64_t)-1440, (int64_t)1440);
    return true;
}

DayPhase DayPhaseController::_phase_at(const Vars::Bank& io, int16_t from_rise,
                                       int16_t from_set) const {
    // Down the day in order, so a boundary an operator has trimmed past the one
    // after it shortens its own phase rather than swallowing the next.
    if (from_rise < io.get(_v.cold_bank_rise)) return PHASE_NIGHT;
    if (from_rise < io.get(_v.breeze_rise))
        return io.get(_v.cold_arm) ? PHASE_COLD_BANK : PHASE_NIGHT;
    if (from_rise < io.get(_v.day_rise))       return PHASE_DAWN_BREEZE;
    if (from_set  < io.get(_v.heat_bank_set))  return PHASE_DAY;
    if (from_set  < io.get(_v.dusk_set))
        return io.get(_v.heat_arm) ? PHASE_HEAT_BANK : PHASE_DAY;
    if (from_set  < io.get(_v.night_set))      return PHASE_DUSK_BREEZE;
    return PHASE_NIGHT;
}

void DayPhaseController::run(ProgramContext& c) {
    const float inside = c.bank.get(_v.inside);

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
        _phase = _phase_at(c.bank, from_rise, from_set);
        c.bank.drive(_v.from_rise, from_rise);
        c.bank.drive(_v.from_set, from_set);
    } else {
        // No location, or no wall clock yet: hold the phase rather than
        // announcing a night the plant is not in. Every boot passes through
        // here, for as long as NTP takes.
        degraded();
    }
    if (_phase == PHASE_DAY && was != PHASE_DAY)     _day_max   = inside;
    if (_phase == PHASE_NIGHT && was != PHASE_NIGHT) _night_min = inside;

    c.bank.drive(_v.phase, (uint8_t)_phase);
    c.bank.drive(_v.prev_day_max, _day_max);
    c.bank.drive(_v.prev_night_min, _night_min);

    // Whether the bank phases have anything to do when their turn comes. Both
    // can be armed at once - a clear late-spring day is hot and its night cold,
    // and the two are decided independently.
    c.bank.drive(_v.cold_arm, _day_max > c.bank.get(_v.cold_bank_trg));
    c.bank.drive(_v.heat_arm, _night_min < c.bank.get(_v.heat_bank_trg));
}

// TargetTempController

void TargetTempController::run(ProgramContext& c) {
    const DayPhase phase = (DayPhase)c.bank.get(_v.phase);
    float target = c.bank.get(_v.cool_set);

    // A bank phase shifts the base only when yesterday justified it: pre-cool
    // before dawn after a hot day, bank heat at dusk after a cold night. In
    // force but not armed, the phase runs at the ordinary target.
    if (phase == PHASE_COLD_BANK && c.bank.get(_v.cold_arm))
        target -= c.bank.get(_v.cold_bank_ofs);
    else if (phase == PHASE_HEAT_BANK && c.bank.get(_v.heat_arm))
        target += c.bank.get(_v.heat_bank_ofs);

    // The clamp is what keeps an aggressively-set offset from pushing the
    // greenhouse somewhere dangerous, whatever the phase asked for.
    c.bank.drive(_v.target, Plc::limit(target, TARGET_MIN, TARGET_MAX));
}

// CoolingController

void CoolingController::run(ProgramContext& c) {
    if (c.first_scan) _pid.reset();   // the integral is a claim about the past

    const bool hi_ok = c.bank.var(_v.ceiling.r).valid();
    const bool lo_ok = c.bank.var(_v.floor.r).valid();
    if (!hi_ok && !lo_ok) {
        // No interior temperature at all. A PID handed a defaulted reading
        // would see 0 degC against the target and call for full HEAT - into a
        // greenhouse nobody can see. Say the demand is unknown instead, and let
        // each actuator go to its own safe state. The window demand goes with
        // it: an opening derived from a temperature nobody has is not an
        // intention, and the windows have their own safe position.
        c.bank.var(_v.demand.r).invalidate(Vars::Q_NO_DATA);
        c.bank.var(_v.window.r).invalidate(Vars::Q_NO_DATA);
        _pid.reset();          // the integral is about a plant we stopped seeing
        faulted();
        return;
    }
    // The ceiling by preference; the floor is a worse place to control from and
    // a better one than nowhere.
    const float inside = hi_ok ? c.bank.get(_v.ceiling) : c.bank.get(_v.floor);
    if (!hi_ok) degraded();

    // The parameters are in the TIME form an operator tunes in (Ti, Td in
    // seconds); PidTuning takes gains. Ti of 0 means no integral action rather
    // than an infinite one, which is what a divide would give.
    const float kp = c.bank.get(_v.kp);
    const float ti = c.bank.get(_v.ti);
    const float td = c.bank.get(_v.td);
    const Plc::PidTuning t(kp,
                           (ti > 0.0f) ? (kp / ti) : 0.0f,
                           kp * td,
                           -1.0f, 1.0f,
                           /* d_tau_s */ 0.0f,
                           /* reverse */ true);

    // Reverse-acting: the demand RISES as the reading does, so above the target
    // it is positive (cooling) and below it negative (the intractors drawing
    // warm floor air, and lower still the thermal screen).
    const float demand = _pid(c.bank.get(_v.target), inside, t, c.dt_ms);
    c.bank.drive(_v.demand, demand);
    // Where the ladder wants the windows, published as the one INTENDED
    // opening: the windows apply the interlocks to it, and an operator can see
    // what was asked for beside where each ram actually is.
    c.bank.drive(_v.window, Plc::ladder(demand, c.bank.get(_v.win_start),
                                        c.bank.get(_v.win_full)));
    _intake_gates(c, inside, demand);
}

void CoolingController::_intake_gates(ProgramContext& c, float inside,
                                      float demand) {
    // Drawing air only moves the interior toward whatever the intake is. Both
    // gates are shut while the crawlspace probe is out, which costs a little
    // ventilation and cannot cost a plant: the windows and the fans are on
    // their own paths.
    const bool intake_ok = c.bank.var(_v.intake.r).valid();
    if (!intake_ok) degraded();
    const float intake = c.bank.get(_v.intake);
    const float margin = c.bank.get(_v.temp_margin);

    c.bank.drive(_v.intake_ok, intake_ok && (inside - intake) >= margin);
    // The mirror, and the only place the negative half of the ladder does
    // anything: cold enough to be asking for heat, and ground warm enough to
    // be worth pulling it out of.
    c.bank.drive(_v.ground_ok, intake_ok &&
                               demand <= c.bank.get(_v.ground_dem) &&
                               (intake - inside) >= margin);
}

// DestratController

void DestratController::run(ProgramContext& c) {
    if (c.first_scan) _bands.reset();

    // A band chosen from a gap nobody measured is a speed chosen from a
    // DEFAULT, and this row's default is 0 - which reads as "no stratification"
    // and stops the fans. Say the demand is unknown instead and let each
    // actuator decide what that means for it.
    if (!c.bank.var(_v.strat.r).valid()) {
        c.bank.var(_v.demand.r).invalidate(Vars::Q_NO_DATA);
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
    const float edge[3] = { c.bank.get(_v.low), c.bank.get(_v.med),
                            c.bank.get(_v.high) };
    const uint8_t speed = _bands(c.bank.get(_v.strat), edge, c.bank.get(_v.hyst));

    // The demand the arbitration reads is the FANS' OWN rung for that speed.
    // Any other number would have to be translated twice, and the two ladders
    // would drift apart the first time one of them was retuned.
    const float rung[4] = { 0.0f, c.bank.get(_v.fan_low),
                            c.bank.get(_v.fan_med), c.bank.get(_v.fan_high) };
    c.bank.drive(_v.demand, rung[speed]);
}

// HumidityController

/** @brief How long the margin must stay above the limit before humidifying is
 *  allowed again. In TIME, because the limit it releases against is already a
 *  parameter: what this adds is that a margin hovering on that limit does not
 *  let the skirt back on every other scan. */
static const uint32_t ROT_RELEASE_MS = 300000;

void HumidityController::observe(ProgramContext& c) {
    if (!c.bank.var(_v.dp_margin.r).valid()) {
        // No margin, no verdict on it - and the guard fails toward acting:
        // ventilating costs energy, not ventilating costs the collection.
        degraded();
        c.bank.drive(_v.vent_dem, 1.0f);
        c.bank.drive(_v.lockout, _lockout_for(true, ROT_RELEASE_MS, c.dt_ms));
        return;
    }

    const float margin = c.bank.get(_v.dp_margin);
    const float limit  = Plc::limit(c.bank.get(_v.rot_limit), ROT_MARGIN_DEGC, 10.0f);

    // A FALLING ramp, negated so the one ladder function serves both
    // directions: nothing at the limit, everything at the dew point itself.
    // The span IS the operator's setting - cli_tooHumid_degC sits on its own
    // floor by default, so a ramp to that floor would be a step.
    c.bank.drive(_v.vent_dem, Plc::ladder(-margin, -limit, 0.0f));

    // An OVERRUN, not a dwell: the lockout follows the condition up at once and
    // outlives it, because humidifying into air that has only just dried out is
    // what puts the margin back where it was. Nothing is locked out at boot.
    c.bank.drive(_v.lockout,
                 _lockout_for(c.bank.get(_v.rot), ROT_RELEASE_MS, c.dt_ms));
    _dry_gate(c);
}

void HumidityController::_dry_gate(ProgramContext& c) {
    // DEW POINTS, not humidities: air at 90% and 5 degC is drier than air at
    // 60% and 25, and swapping one for the other is what ventilating does.
    const bool have = c.bank.var(_v.in_t.r).valid() &&
                      c.bank.var(_v.in_rh.r).valid() &&
                      c.bank.var(_v.ext_t.r).valid() &&
                      c.bank.var(_v.ext_rh.r).valid();
    if (!have) {
        // Shut, and said: opening on a guess would mean drawing in air that may
        // be wetter than what is already here, which is the one thing this
        // gate exists to prevent. The rot ladder is not gated and still vents.
        degraded();
        c.bank.drive(_v.dry_ok, false);
        return;
    }
    const float inside  = Plc::dew_point(c.bank.get(_v.in_t), c.bank.get(_v.in_rh));
    const float outside = Plc::dew_point(c.bank.get(_v.ext_t), c.bank.get(_v.ext_rh));
    c.bank.drive(_v.dry_ok, (inside - outside) >= c.bank.get(_v.dry_margin));
}

void HumidityController::run(ProgramContext& c) {
    // Four bands and a dead zone, and the signed ladder IS that shape: below
    // dehumStart the margin is closing on condensation (negative, dehumidify);
    // above humidStart the air is drier than the plants want (positive); in
    // between nothing moves.
    const float m = c.bank.get(_v.dp_margin);
    c.bank.drive(_v.demand, Plc::signed_ladder(m,
                                             c.bank.get(_v.dehum_full),
                                             c.bank.get(_v.dehum_start),
                                             c.bank.get(_v.humid_start),
                                             c.bank.get(_v.humid_full)));
}

}  // namespace Site
