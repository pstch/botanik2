/**
 * @file tasks_irrigation.cpp
 * @brief Irrigation: the modulated valve, and the tray cycle that serves one
 *        tray at a time.
 *
 * The interfaces are in tasks.h; what each law does is here, so the header
 * reads as the list of programs and their variables.
 */

#include <cmath>

#include "libbotanik/utils/time.h"

#include "tasks.h"

namespace Site {

using namespace Botanik;

/** @brief How far the outlet flow has to move to be worth a status write, in
 *  Lpm. About the METER's noise, not about the plant. */
static const float IRR_FLOW_BAND = 0.1f;

/** @brief Window the inlet counter is differentiated over, in ms. A pulse is a
 *  whole litre, so scan-to-scan the rate would alternate between zero and a
 *  spike; a window long enough to hold several pulses reads as a flow. */
static const uint32_t IRR_RATE_WINDOW_MS = 5000;

/** @brief Below this the outlet is idle, in Lpm. A loop sitting on its own
 *  noise would otherwise totalize a plausible volume with the valves shut. */
static const float IRR_FLOW_CUTOFF = 0.2f;

// ValveController

void ValveController::attach(const Vars::Bank& io, Vars::RefOut<bool> coil) {
    _coil = coil;
    // The declared safe state IS the fail direction: a de-energised coil is
    // the only thing a dead controller guarantees, and the row already says
    // which way that leaves this valve.
    const Vars::Descriptor& d = io.descriptor(coil.r.index);
    _fail_open = Vars::as_bool(d.type, d.def);
}

float ValveController::modulate(Vars::Bank& io, float duty, uint32_t period_ms,
                                uint32_t min_on_ms, uint32_t dt_ms) {
    duty = Plc::limit(duty, 0.0f, 1.0f);
    if (period_ms == 0) period_ms = 1;

    const uint32_t on_ms = (uint32_t)(duty * (float)period_ms);
    // A pulse under the minimum does not lift the seat, so it is not made:
    // rounding it up instead would deliver water nobody asked for.
    const uint32_t served = (on_ms < min_on_ms) ? 0 : on_ms;

    _phase_ms += dt_ms;
    while (_phase_ms >= period_ms) _phase_ms -= period_ms;

    set(io, _phase_ms < served);
    return (float)served / (float)period_ms;
}

// IrrigationController

const uint64_t IrrigationController::FOLD_MS = 3600ull * 1000ull;

void IrrigationController::bind(Vars::Bank& io) {
    _v.bind(io, *this);
    for (uint8_t i = 0; i < 2; i++) {
        _inlet_v[i].attach(io, _v.inlet[i]);
        _outlet_v[i].attach(io, _v.outlet[i]);
    }
    for (uint8_t i = 0; i < IRR_TRAYS; i++) _tray_v[i].attach(io, _v.tray_valve[i]);
}

float IrrigationController::_track_dryness(ProgramContext& c) {
    // Once an HOUR, and the interval is the whole design: folded every scan the
    // increment underflows a float and the average freezes; folded once a day
    // the answer depends on which hour it was taken.
    if (c.now_ms - _last_fold_ms >= FOLD_MS || !_dryness_seeded) {
        const uint16_t days = c.bank.get(_v.dry_days);
        const float alpha = (days == 0) ? 1.0f : 1.0f / (24.0f * (float)days);
        _dryness.configure(alpha, alpha);
        const float now = c.bank.get(_v.vpd);
        if (!_dryness_seeded) { _dryness.preset(now); _dryness_seeded = true; }
        else                  { _dryness.push(now); }
        _last_fold_ms = c.now_ms;
    }
    c.bank.drive(_v.vpd_avg, _dryness.value());
    return _dryness.value();
}

float IrrigationController::_modulation(const Vars::Bank& io, float vpd_avg) const {
    const float demand = Plc::ladder(vpd_avg, io.get(_v.dry_lo), io.get(_v.dry_hi));
    const float mod_min = Plc::limit(io.get(_v.mod_min), 0.0f, 1.0f);
    // Gamma is held at 1 or more so the curve is concave-up: watering reaches
    // its summer figure only once dryness is really established, and every
    // error in between is toward LESS water.
    const float gamma = (io.get(_v.mod_gamma) < 1.0f) ? 1.0f : io.get(_v.mod_gamma);
    return Plc::limit(mod_min + (1.0f - mod_min) * powf(demand, gamma),
                      mod_min, 1.0f);
}

uint32_t IrrigationController::_today() {
    // The floor the sun tracker uses: below it the RTC has never been set, so
    // there is no date and nothing may be scheduled against one.
    const uint64_t wall = HAL::Clock::wall_ms();
    if (wall < 1600000000000ull) return 0;
    return Utils::cal_time_ms(wall).unixDays;
}

uint8_t IrrigationController::_next_due(const Vars::Bank& io, float mod,
                                        uint32_t today) const {
    // period = summer period / mod^(1 - split), volume = summer volume *
    // mod^split, so their RATIO - the water actually delivered per day - is
    // proportional to mod whatever the split is set to.
    const float split = Plc::limit(io.get(_v.split), 0.0f, 1.0f);
    const float stretch = powf(mod, 1.0f - split);

    uint8_t  best = 0;
    uint32_t worst_overdue = 0;
    for (uint8_t i = 0; i < IRR_TRAYS; i++) {
        if (io.get(_v.force_off[i])) continue;        // held back by hand
        if (io.get(_v.force_on[i]))  return (uint8_t)(i + 1);
        if (today == 0) continue;                     // no date, no schedule
        const float summer_days = (float)io.get(_v.days[i]);
        if (summer_days <= 0.0f) continue;            // this tray is not scheduled

        // Damper air stretches the interval; drier air shortens it back toward
        // the summer figure. One day is the floor, since a cycle is a day's work.
        const float period = (stretch > 0.0f) ? (summer_days / stretch)
                                              : summer_days;
        const uint32_t due_after = (period < 1.0f) ? 1u
                                                   : (uint32_t)lroundf(period);

        const uint32_t last = io.get(_v.last[i]);
        // A tray never served has no history to be overdue against, so it goes
        // first; one whose stored day is in the FUTURE is a clock that moved
        // backwards, and waiting for it to catch up would strand the tray.
        const uint32_t elapsed = (last == 0 || last > today)
                                     ? due_after : (today - last);
        if (elapsed < due_after) continue;

        // The most overdue first, so a backlog drains in the order it built up
        // rather than by tray number.
        const uint32_t overdue = elapsed - due_after;
        if (best == 0 || overdue > worst_overdue) {
            best = (uint8_t)(i + 1);
            worst_overdue = overdue;
        }
    }
    return best;
}

void IrrigationController::_set_valves(Vars::Bank& io, IrrState state, uint8_t tray) {
    // Fill: inlet open, this tray open, outlet shut. Drain: inlet shut, tray
    // still open, outlet open. Every other tray is shut in both, so one cycle
    // can never water two.
    const bool filling  = (state == IRR_FILLING);
    const bool draining = (state == IRR_DRAINING);
    for (uint8_t i = 0; i < 2; i++) {
        _inlet_v[i].set(io, filling || state == IRR_FLUSHING);
        _outlet_v[i].set(io, draining || state == IRR_FLUSHING);
    }
    for (uint8_t i = 0; i < IRR_TRAYS; i++)
        _tray_v[i].set(io, (filling || draining) && (i + 1 == tray));
}

void IrrigationController::_blame(Vars::Bank& io,
                                  const Vars::RefOut<bool>* per_tray, bool on) {
    // The tray being served, and only it. The any-form beside these is what an
    // operator sees; this is what still says WHICH tray once the cycle has
    // moved on to the next one.
    if (_tray >= 1 && _tray <= IRR_TRAYS) io.drive(per_tray[_tray - 1], on);
}

void IrrigationController::_park_valves(Vars::Bank& io) {
    for (uint8_t i = 0; i < 2; i++) {
        _inlet_v[i].park(io);
        _outlet_v[i].park(io);
    }
    for (uint8_t i = 0; i < IRR_TRAYS; i++) _tray_v[i].park(io);
}

float IrrigationController::_delivered_litres(const Vars::Bank& io) const {
    const HAL::Pulse::Sample s = HAL::Pulse::sample(Site::pulse_inputs[0].channel);
    // Unsigned difference, so the 32-bit counter wrapping costs nothing.
    const uint32_t edges = s.count - _fill_start_pulses;
    return (float)edges * io.get(_v.qty_per_pulse);
}

void IrrigationController::_stamp_served(Vars::Bank& io) {
    const uint32_t day = _today();
    if (day == 0 || _tray == 0) return;

    // AFTER THE FILL, not after the drain: the tray has had its water by then,
    // and a fault or a restart during the long drain must not water it twice.
    // A forced cycle stamps too - it IS a watering, and leaving it unrecorded
    // would water the tray again on its own account tomorrow.
    //
    // Through param_set(), because the row is a PARAMETER: drive() writes the
    // value but skips the unsaved mark, so the schedule would restart from
    // nothing at the next boot. Once a cycle is a rate flash is happy with.
    Var h = io.var(_v.last[_tray - 1].r);
    if (io.param_set_i(h, (int64_t)day) == SET_OK) (void)io.param_save(h);
    if (_served < 0xFFu) _served++;
}

void IrrigationController::observe(ProgramContext& c) {
    const uint32_t pulses = HAL::Pulse::sample(Site::pulse_inputs[0].channel).count;
    if (c.first_scan) {
        _out_flow.reset(); _out_band.reset(); _in_rate.reset();
        // Adopt both counters where they stand rather than starting from zero:
        // the retained rows carry what was metered before this run, and the
        // pulse counter carries what has been metered since the last boot.
        _in_base_L      = c.bank.get(_v.in_total);
        _in_base_pulses = pulses;
        _out_meter.preset(c.bank.get(_v.out_total));
    }

    // Published whatever the cycle is doing: a meter reading is an observation,
    // and one that froze the moment the plant parked would look live.
    const float lpm = _out_flow(c.bank.get(_v.out_flow),
                                0.001f * c.bank.get(_v.out_avg_ms), c.dt_ms);
    c.bank.drive(_v.out_flow_pub, _out_band(lpm, IRR_FLOW_BAND));
    // Per SECOND into the totalizer, which is what the block counts in.
    c.bank.drive(_v.out_total, _out_meter(lpm / 60.0f, c.dt_ms,
                                          IRR_FLOW_CUTOFF / 60.0f));

    // Unsigned difference, so the 32-bit counter wrapping costs nothing.
    const float in_total = _in_base_L + (float)(pulses - _in_base_pulses)
                                        * c.bank.get(_v.qty_per_pulse);
    c.bank.drive(_v.in_total, in_total);
    const float in_flow = 60.0f * _in_rate(in_total, IRR_RATE_WINDOW_MS, c.dt_ms);
    c.bank.drive(_v.in_flow, in_flow);
    _water_alarms(c, in_flow);
}

bool IrrigationController::_in_slot(const Vars::Bank& io) const {
    const DayPhase phase = (DayPhase)io.get(_v.phase);

    // END OF NIGHT, and only when the plants are not cold: watering cold roots
    // is what the gate exists to prevent, and the morning slot below is where
    // that watering goes instead.
    if (phase == PHASE_COLD_BANK || phase == PHASE_DAWN_BREEZE)
        return io.get(_v.outside_temp) >= io.get(_v.dawn_gate);

    // START OF THE DAY, for a window an operator sets: the tray has to drain
    // and dry before the stomata open at dusk.
    return phase == PHASE_DAY &&
           io.get(_v.from_rise) <= (int16_t)(io.get(_v.day_rise) +
                                             (int16_t)io.get(_v.morning_win));
}

void IrrigationController::_water_alarms(ProgramContext& c, float in_flow) {
    // Idle means every valve in the circuit is shut, so anything the inlet
    // meter counts is going somewhere nobody opened.
    c.bank.drive(_v.leak, _state == IRR_IDLE &&
                          in_flow > c.bank.get(_v.leak_Lpm));

    // The mirror image: the inlet is open and the water is not arriving. Only
    // once the rate window has had time to fill, or every fill would start by
    // announcing a pressure fault.
    const bool filling = _state == IRR_FILLING &&
                         _in_state_ms > 2u * IRR_RATE_WINDOW_MS;
    c.bank.drive(_v.pressure, filling &&
                              in_flow < c.bank.get(_v.min_fill_Lpm));
}

void IrrigationController::_accumulate_drain(ProgramContext& c) {
    if (!c.bank.var(_v.out_flow.r).valid()) {
        // No verdict is possible without the meter, and the window still has to
        // end: _drain_verdict() sees a total of zero against a live dose and
        // declines rather than blaming the tray.
        degraded();
        return;
    }
    // The AVERAGE, which observe() folded on the previous activation: one beat
    // of lag over a window measured in minutes.
    _drained_L += _out_flow.value() * (float)c.dt_ms / 60000.0f;
    c.bank.drive(_v.drained, _drained_L);
}

void IrrigationController::_drain_verdict(Vars::Bank& io) {
    const bool measured = io.var(_v.out_flow.r).valid();
    if (_dose_L <= 0.0f || !measured) {
        // A fill that never delivered, or a meter that is not answering. Both
        // are already alarms of their own; a clog raised on top would send
        // somebody to the wrong end of the plant.
        io.drive(_v.drain_fault, false);
        _blame(io, _v.drain_tray, false);
        return;
    }
    // Against the dose, not against a fixed volume: a modulated half-dose
    // drains half as much and is not a clog.
    const float frac     = _drained_L / _dose_L;
    const float min_frac = io.get(_v.exp_drain) - io.get(_v.drain_margin);
    const bool  clog     = frac < min_frac;
    io.drive(_v.drain_fault, clog);
    _blame(io, _v.drain_tray, clog);
}

void IrrigationController::_enter(Vars::Bank& io, IrrState state) {
    _state = state;
    _in_state_ms = 0;
    if (state == IRR_FILLING)
        _fill_start_pulses = HAL::Pulse::sample(Site::pulse_inputs[0].channel).count;
    if (state == IRR_DRAINING) {
        _drained_L = 0.0f;
        io.drive(_v.drained, 0.0f);
    }
    _set_valves(io, state, _tray);
    io.drive(_v.state, (uint8_t)state);
    io.drive(_v.active, _tray);
}

void IrrigationController::run(ProgramContext& c) {
    if (inhibit_requested()) {
        // Where a dead controller would leave them, which is not the same for
        // all ten: the inlet fails SHUT and the drains fail OPEN, so a tray
        // that was filling empties rather than sitting full.
        _park_valves(c.bank);
        _tray  = 0;
        _state = IRR_IDLE;
        c.bank.drive(_v.state, (uint8_t)IRR_IDLE);
        c.bank.drive(_v.active, (uint8_t)0);
        return;
    }
    if (c.first_scan) {
        // Control was away, so whatever was open is not ours to assume. Shut
        // everything and start the cycle again rather than resuming a fill
        // whose delivered volume we stopped counting.
        _tray = 0;
        _enter(c.bank, IRR_IDLE);
    }
    _in_state_ms += c.dt_ms;

    const float mod = _modulation(c.bank, _track_dryness(c));
    c.bank.drive(_v.mod, mod);

    // The daily tally is against a DATE, not an uptime: it rolls when the day
    // does, so a cycle running through midnight is not counted twice and a
    // reboot at noon does not hand the plant a second allowance.
    const uint32_t today = _today();
    if (today != 0 && today != _served_day) {
        _served_day = today;
        _served     = 0;
    }
    const float split = Plc::limit(c.bank.get(_v.split), 0.0f, 1.0f);
    const float dose = (_tray > 0)
        ? (float)c.bank.get(_v.litres[_tray - 1]) * powf(mod, split) : 0.0f;
    c.bank.drive(_v.volume, dose);

    switch (_state) {
        case IRR_IDLE: {
            c.bank.drive(_v.delivered, 0.0f);
            if (_served >= c.bank.get(_v.trays_per_day) && !c.bank.get(_v.force_now))
                break;                         // the surplus waits for tomorrow
            // WHEN, before which tray: the climate decides the slot and the
            // schedule decides the tray, and an operator forcing a cycle
            // overrides both.
            if (!c.bank.get(_v.force_now) && !_in_slot(c.bank)) break;

            const uint8_t next = _next_due(c.bank, mod, today);
            if (next == 0) break;
            _tray   = next;
            _forced = c.bank.get(_v.force_now);
            _enter(c.bank, IRR_FILLING);
            break;
        }
        case IRR_FILLING: {
            const float got = _delivered_litres(c.bank);
            c.bank.drive(_v.delivered, got);
            if (got >= dose && dose > 0.0f) {
                c.bank.drive(_v.fill_fault, false);
                _blame(c.bank, _v.fill_tray, false);
                _stamp_served(c.bank);
                _dose_L = got;
                _enter(c.bank, IRR_DRAINING);
            } else if (_in_state_ms >= 1000u * c.bank.get(_v.max_fill_s)) {
                // The dose never arrived: water off, a stuck inlet, or a meter
                // that has stopped counting. Move to drain regardless - leaving
                // the inlet open on a fault is how a tray overflows.
                c.bank.drive(_v.fill_fault, true);
                _blame(c.bank, _v.fill_tray, true);
                // What actually went in, which may be nothing: the drain is
                // judged against the water the tray really got.
                _dose_L = got;
                _enter(c.bank, IRR_DRAINING);
            }
            break;
        }
        case IRR_DRAINING: {
            _accumulate_drain(c);
            if (_in_state_ms < 1000u * c.bank.get(_v.drain_window_s)) break;
            _drain_verdict(c.bank);
            _tray = 0;
            _enter(c.bank, IRR_FLUSHING);
            break;
        }
        case IRR_FLUSHING: {
            if (_in_state_ms >= 1000u * c.bank.get(_v.flush_s)) _enter(c.bank, IRR_IDLE);
            break;
        }
    }
    c.bank.drive(_v.served, _served);
}

}  // namespace Site
