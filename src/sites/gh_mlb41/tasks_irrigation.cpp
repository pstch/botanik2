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

#include "tasks_irrigation.h"

namespace Site {

using namespace Botanik;

/** @brief How far the outlet flow has to move to be worth a status write, in
 *  Lpm. About the METER's noise, not about the plant - so it is also the
 *  distance from zero within which the row simply reads zero. */
static const float IRR_FLOW_BAND = 0.1f;

/** @brief Window the inlet counter is differentiated over, in ms. A pulse is a
 *  whole litre, so scan-to-scan the rate would alternate between zero and a
 *  spike; a window long enough to hold several pulses reads as a flow. */
static const uint32_t IRR_RATE_WINDOW_MS = 5000;

/** @brief Below this the outlet is idle, in Lpm. A loop sitting on its own
 *  noise would otherwise totalize a plausible volume with the valves shut. */
static const float IRR_FLOW_CUTOFF = 0.2f;

// ValveController

void ValveController::attach(Vars::RefOut<bool> coil) {
    _coil = coil;
    // The declared safe state IS the fail direction: a de-energised coil is
    // the only thing a dead controller guarantees, and the row already says
    // which way that leaves this valve.
    const Vars::Descriptor& d = coil.io->descriptor(coil.r.index);
    _fail_open = Vars::as_bool(d.type, d.def);
}

float ValveController::modulate(float duty, uint32_t period_ms,
                                uint32_t min_on_ms, uint32_t dt_ms) {
    duty = Plc::limit(duty, 0.0f, 1.0f);
    if (period_ms == 0) period_ms = 1;

    const uint32_t on_ms = (uint32_t)(duty * (float)period_ms);
    // A pulse under the minimum does not lift the seat, so it is not made:
    // rounding it up instead would deliver water nobody asked for.
    const uint32_t served = (on_ms < min_on_ms) ? 0 : on_ms;

    _phase_ms += dt_ms;
    while (_phase_ms >= period_ms) _phase_ms -= period_ms;

    set(_phase_ms < served);
    return (float)served / (float)period_ms;
}

// IrrigationModulation

const uint64_t IrrigationModulation::FOLD_MS = 3600ull * 1000ull;

float IrrigationModulation::_track_dryness(ProgramContext& c) {
    // Once an HOUR, and the interval is the whole design: folded every scan the
    // increment underflows a float and the average freezes; folded once a day
    // the answer depends on which hour it was taken.
    if (c.now_ms - _last_fold_ms >= FOLD_MS || !_seeded) {
        const uint16_t days = _v.dry_days.get();
        const float alpha = (days == 0) ? 1.0f : 1.0f / (24.0f * (float)days);
        _dryness.configure(alpha, alpha);
        const float now = _v.vpd.get();
        if (!_seeded) { _dryness.preset(now); _seeded = true; }
        else          { _dryness.push(now); }
        _last_fold_ms = c.now_ms;
    }
    _v.vpd_avg.set(_dryness.value());
    return _dryness.value();
}

void IrrigationModulation::observe(ProgramContext& c) {
    const float vpd_avg = _track_dryness(c);

    const float demand = Plc::ladder(vpd_avg, _v.dry_lo.get(),
                                     _v.dry_hi.get());
    const float mod_min = Plc::limit(_v.mod_min.get(), 0.0f, 1.0f);
    // Gamma is held at 1 or more so the curve is concave-up: watering reaches
    // its summer figure only once dryness is really established, and every
    // error in between is toward LESS water.
    const float gamma = Plc::max_of(1.0f, _v.mod_gamma.get());
    _v.mod.set(Plc::limit(mod_min + (1.0f - mod_min) * powf(demand, gamma),
                          mod_min, 1.0f));
}

// WaterMeters

void WaterMeters::observe(ProgramContext& c) {
    const uint8_t  ch = Site::pulse_inputs[0].channel;
    const uint32_t pulses = HAL::Pulse::sample(ch).count;
    if (c.first_scan) {
        _out_flow.reset(); _out_band.reset(); _in_rate.reset();
        // Adopt both counters where they stand rather than starting from zero:
        // the retained rows carry what was metered before this run, and the
        // pulse counter carries what has been metered since the last boot.
        _in_base_L      = _v.in_total.get();
        _in_base_pulses = pulses;
        _out_meter.preset(_v.out_total.get());
    }

    const float lpm = _out_flow(_v.out_flow.get(),
                                0.001f * (float)_v.out_avg_ms.get(), c.dt_ms);
    if (!_v.out_flow.valid()) {
        // A flow derived from a loop that has stopped answering is not a flow.
        // The TOTAL beside it keeps its value: a totalizer is a record of what
        // went past, and the instrument failing does not unmake it.
        // The last flow the loop reported, held: a totalizer beside it that
        // stopped counting says the same thing, and zero would read as "no
        // water is moving", which is what a leak detector must not be told.
        _v.out_flow_pub.hold();
        degraded();
    } else if (lpm < IRR_FLOW_BAND) {
        // WITHIN THE DEADBAND OF ZERO, so it reads zero: a reading the band
        // cannot tell from nothing is nothing. Left to the block the row parks
        // on the last tenth it reported - a drain meter saying 0.09 Lpm with
        // every valve shut, which is what a leak looks like. Reset with it, so
        // the next real flow is written at once rather than waiting out a band
        // from a figure the plant has left behind.
        _out_band.reset();
        _v.out_flow_pub.set(0.0f);
    } else {
        _v.out_flow_pub.set(_out_band(lpm, IRR_FLOW_BAND));
    }
    // Per SECOND into the totalizer, which is what the block counts in.
    _v.out_total.set(_out_meter(lpm / 60.0f, c.dt_ms,
                                          IRR_FLOW_CUTOFF / 60.0f));

    // Unsigned difference, so the 32-bit counter wrapping costs nothing.
    const float in_total = _in_base_L + (float)(pulses - _in_base_pulses)
                                        * _v.qty_per_pulse.get();
    _v.in_total.set(in_total);
    const float in_flow =
        60.0f * _in_rate(in_total, IRR_RATE_WINDOW_MS, c.dt_ms);
    _v.in_flow.set(in_flow);
    _supply_alarms(c, in_flow);
}

void WaterMeters::_supply_alarms(ProgramContext& c, float in_flow) {
    const IrrState state = (IrrState)_v.state.get();

    // Idle means every valve in the circuit is shut, so anything the inlet
    // meter counts is going somewhere nobody opened.
    _v.leak.set(state == IRR_IDLE &&
                          in_flow > _v.leak_Lpm.get());

    // The mirror image: the inlet is open and the water is not arriving. The
    // row's own dwell covers the start of a fill, where the rate window has not
    // filled yet and every fill would otherwise open with a pressure fault.
    _v.pressure.set(state == IRR_FILLING &&
                              in_flow < _v.min_fill_Lpm.get());
}

// IrrigationCycle

void IrrigationCycle::bind(Vars::Bank& io) {
    _v.bind(io, *this);
    for (uint8_t i = 0; i < 2; i++) {
        _inlet_v[i].attach(_v.inlet[i]);
        _outlet_v[i].attach(_v.outlet[i]);
    }
    for (uint8_t i = 0; i < IRR_TRAYS; i++) _tray_v[i].attach(_v.tray_valve[i]);
}

uint32_t IrrigationCycle::_today() {
    // The floor the sun tracker uses: below it the RTC has never been set, so
    // there is no date and nothing may be scheduled against one.
    const uint64_t wall = HAL::Clock::wall_ms();
    if (wall < 1600000000000ull) return 0;
    // Straight off the wall clock, NOT through cal_time_ms(): that takes an
    // UPTIME-domain instant and projects it onto the wall, so handing it a wall
    // time adds the boot epoch a second time. The date came out around 2083 and
    // gained a day every twelve hours, which halves every tray's interval.
    return (uint32_t)(wall / 86400000ull);
}

uint8_t IrrigationCycle::_next_due(float mod, uint32_t today) {
    // period = summer period / mod^(1 - split), volume = summer volume *
    // mod^split, so their RATIO - the water actually delivered per day - is
    // proportional to mod whatever the split is set to.
    const float split = Plc::limit(_v.split.get(), 0.0f, 1.0f);
    const float stretch = powf(mod, 1.0f - split);

    uint8_t  best = 0;
    uint32_t worst_overdue = 0;
    for (uint8_t i = 0; i < IRR_TRAYS; i++) {
        if (_v.force_on[i].get()) return (uint8_t)(i + 1);
        if (today == 0) continue;                     // no date, no schedule
        const float summer_days = (float)_v.days[i].get();
        if (summer_days <= 0.0f) continue;        // this tray is not scheduled

        // Damper air stretches the interval; drier air shortens it back toward
        // the summer figure. One day is the floor: a cycle is a day's work.
        const float period = (stretch > 0.0f) ? (summer_days / stretch)
                                              : summer_days;
        const uint32_t due_after = (period < 1.0f) ? 1u
                                                   : (uint32_t)lroundf(period);

        const uint32_t last = _v.last[i].get();
        // A tray never served has no history to be overdue against, so it goes
        // first; one whose stored day is in the FUTURE is a clock that moved
        // backwards, and waiting for it to catch up would strand the tray.
        const uint32_t elapsed = (last == 0 || last > today)
                                     ? due_after : (today - last);
        if (elapsed < due_after) continue;

        // Held back by hand, and ONCE: the skip is spent on the watering it
        // was due for, and the tray goes to the back of the queue rather than
        // arriving again tomorrow with the flag gone.
        if (_v.force_off[i].get()) {
            _clear_force(_v.force_off[i]);
            _stamp(i, today);
            continue;
        }

        // The most overdue first, so a backlog drains in the order it built up
        // rather than by tray number.
        const uint32_t overdue = elapsed - due_after;
        if (best == 0 || overdue > worst_overdue) {
            best = (uint8_t)(i + 1);
            worst_overdue = overdue;
            // The interval that chose it, published: the summer figure is on
            // the PARAMS page and this is what the weather made of it.
            _v.period.set(period);
        }
    }
    return best;
}

void IrrigationCycle::_set_valves(IrrState state, uint8_t tray) {
    // Fill: inlet open, this tray open, outlet shut. Drain: inlet shut, tray
    // still open, outlet open. Every other tray is shut in both, so one cycle
    // can never water two.
    const bool filling  = (state == IRR_FILLING);
    const bool draining = (state == IRR_DRAINING);
    // FLUSHING opens BOTH ends with no tray between them: fresh water runs the
    // length of the shared manifold and out through the drain, so what sat in
    // the line after a tray emptied is not what the next tray gets first.
    for (uint8_t i = 0; i < 2; i++) {
        _inlet_v[i].set(filling || state == IRR_FLUSHING);
        _outlet_v[i].set(draining || state == IRR_FLUSHING);
    }
    for (uint8_t i = 0; i < IRR_TRAYS; i++)
        _tray_v[i].set((filling || draining) && (i + 1 == tray));
}

void IrrigationCycle::_blame(const Vars::RefOut<bool>* per_tray, bool on) {
    // The tray being served, and only it. The any-form beside these is what an
    // operator sees; this is what still says WHICH tray once the cycle has
    // moved on to the next one.
    if (_tray >= 1 && _tray <= IRR_TRAYS) per_tray[_tray - 1].set(on);
}

void IrrigationCycle::_park_valves() {
    for (uint8_t i = 0; i < 2; i++) {
        _inlet_v[i].park();
        _outlet_v[i].park();
    }
    for (uint8_t i = 0; i < IRR_TRAYS; i++) _tray_v[i].park();
}

void IrrigationCycle::_stamp(uint8_t tray, uint32_t day) const {
    // An EDIT, not a drive: the row is a PARAMETER, and driving it would write
    // the value while leaving flash owing nothing - so the schedule would
    // restart from nothing at the next boot. Once a cycle is a rate flash is
    // happy with.
    (void)_v.last[tray].edit((uint32_t)day);
}

void IrrigationCycle::_clear_force(Vars::RefInOut<bool> r) {
    (void)r.edit(false);
}

void IrrigationCycle::_stamp_served() {
    if (_tray == 0) return;

    // Both forces are spent on the watering they asked for, and spent WHATEVER
    // the clock says. Left standing, one asks for the same tray on every cycle
    // and the other for a cycle at any hour - for ever, and no other tray is
    // ever reached. Before NTP there is no date to stamp, and the tally below
    // is then the only thing bounding the day, so neither may wait on one.
    _clear_force(_v.force_on[_tray - 1]);
    if (_forced) { _clear_force(_v.force_now); _forced = false; }
    if (_served < 0xFFu) _served++;

    // AFTER THE FILL, not after the drain: the tray has had its water by then,
    // and a fault or a restart during the long drain must not water it twice.
    // A forced cycle stamps too - it IS a watering, and leaving it unrecorded
    // would water the tray again on its own account tomorrow.
    const uint32_t day = _v.today.get();
    if (day != 0) _stamp((uint8_t)(_tray - 1), day);
}

bool IrrigationCycle::_in_slot() const {
    const DayPhase phase = (DayPhase)_v.phase.get();

    // END OF NIGHT, and only when the plants are not cold: watering cold roots
    // is what the gate exists to prevent, and the morning slot below is where
    // that watering goes instead.
    if (phase == PHASE_COLD_BANK || phase == PHASE_DAWN_BREEZE)
        return _v.outside_temp.get() >= _v.dawn_gate.get();

    // START OF THE DAY, for a window an operator sets: the tray has to drain
    // and dry before the stomata open at dusk.
    return phase == PHASE_DAY &&
           _v.from_rise.get() <= (int16_t)(_v.day_rise.get() +
                                             (int16_t)_v.morning_win.get());
}

void IrrigationCycle::_drain_verdict() {
    const bool measured = _v.out_flow.valid();
    if (_dose_L <= 0.0f || !measured) {
        // A fill that never delivered, or a meter that is not answering. Both
        // are already alarms of their own; a clog raised on top would send
        // somebody to the wrong end of the plant.
        _v.drain_fault.set(false);
        _blame(_v.drain_tray, false);
        return;
    }
    // Against the dose, not against a fixed volume: a modulated half-dose
    // drains half as much and is not a clog.
    // The published row, not the difference behind it: a verdict made on a
    // number nobody can see is a verdict nobody can rehearse.
    const float frac     = _v.drained.get() / _dose_L;
    const float min_frac = _v.exp_drain.get() - _v.drain_margin.get();
    const bool  clog     = frac < min_frac;
    _v.drain_fault.set(clog);
    _blame(_v.drain_tray, clog);
}

void IrrigationCycle::_enter(IrrState state) {
    _state = state;
    _in_state_ms = 0;
    // The meters are counters, so a window is a DIFFERENCE: stamp the one the
    // next state is judged on and read it again when the state ends.
    if (state == IRR_FILLING) _fill_start_L = _v.in_total.get();
    if (state == IRR_DRAINING) {
        _drain_start_L = _v.out_total.get();
        _v.drained.set(0.0f);
    }
    _set_valves(state, _tray);
    _v.state.set((uint8_t)state);
    _v.active.set(_tray);
}

void IrrigationCycle::run(ProgramContext& c) {
    if (inhibit_requested()) {
        // Where a dead controller would leave them, which is not the same for
        // all ten: the inlet fails SHUT and the drains fail OPEN, so a tray
        // that was filling empties rather than sitting full.
        _park_valves();
        _tray  = 0;
        _state = IRR_IDLE;
        _v.state.set((uint8_t)IRR_IDLE);
        _v.active.set((uint8_t)0);
        return;
    }
    if (c.first_scan) {
        // Control was away, so whatever was open is not ours to assume. Shut
        // everything and start the cycle again rather than resuming a fill
        // whose delivered volume we stopped counting.
        _tray = 0;
        _enter(IRR_IDLE);
    }
    _in_state_ms += c.dt_ms;

    // The dose is the tray's summer figure scaled by this: without it there is
    // no dose to fill to, and a cycle started anyway would run its inlet to the
    // fill timeout and blame the tray for it.
    const bool  can_dose = _v.mod.valid();
    const float mod      = _v.mod.get();

    // The daily tally is against a DATE, not an uptime: it rolls when the day
    // does, so a cycle running through midnight is not counted twice and a
    // reboot at noon does not hand the plant a second allowance.
    //
    // Published and read back, like every other decision this law makes: the
    // date is what the whole schedule turns on, and one nobody can see is one
    // nobody can rehearse.
    _v.today.set(_today());
    const uint32_t today = _v.today.get();
    if (today != 0 && today != _served_day) {
        _served_day = today;
        _served     = 0;
    }
    const float split = Plc::limit(_v.split.get(), 0.0f, 1.0f);
    const float dose = (_tray > 0)
        ? (float)_v.litres[_tray - 1].get() * powf(mod, split) : 0.0f;
    if (can_dose) _v.volume.set(dose);
    else          _v.volume.hold();

    switch (_state) {
        case IRR_IDLE: {
            _v.delivered.set(0.0f);
            if (_served >= _v.trays_per_day.get() && !_v.force_now.get())
                break;                         // the surplus waits for tomorrow
            // WHEN, before which tray: the climate decides the slot and the
            // schedule decides the tray, and an operator forcing a cycle
            // overrides both.
            if (!_v.force_now.get() && !_in_slot()) break;
            if (!can_dose) break;

            const uint8_t next = _next_due(mod, today);
            if (next == 0) break;
            _tray   = next;
            _forced = _v.force_now.get();
            _enter(IRR_FILLING);
            break;
        }
        case IRR_FILLING: {
            _v.delivered.set(_v.in_total.get() - _fill_start_L);
            // ...and read back, so the fill ends on the row an operator can
            // watch and force rather than on a local difference.
            const float got = _v.delivered.get();
            if (got >= dose && dose > 0.0f) {
                _v.fill_fault.set(false);
                _blame(_v.fill_tray, false);
                _stamp_served();
                _dose_L = got;
                _enter(IRR_DRAINING);
            } else if (_in_state_ms >= 1000u * _v.max_fill_s.get()) {
                // The dose never arrived: water off, a stuck inlet, or a meter
                // that has stopped counting. Move to drain regardless - leaving
                // the inlet open on a fault is how a tray overflows.
                _v.fill_fault.set(true);
                _blame(_v.fill_tray, true);
                // What actually went in, which may be nothing: the drain is
                // judged against the water the tray really got.
                _dose_L = got;
                _enter(IRR_DRAINING);
            }
            break;
        }
        case IRR_DRAINING: {
            _v.drained.set(_v.out_total.get() - _drain_start_L);
            if (_in_state_ms < 1000u * _v.drain_window_s.get()) break;
            _drain_verdict();
            _tray = 0;
            _enter(IRR_FLUSHING);
            break;
        }
        case IRR_FLUSHING: {
            if (_in_state_ms >= 1000u * _v.flush_s.get()) _enter(IRR_IDLE);
            break;
        }
    }
    _v.served.set(_served);
}

}  // namespace Site
