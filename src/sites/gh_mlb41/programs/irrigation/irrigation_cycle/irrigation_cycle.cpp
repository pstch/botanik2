/**
 * @file irrigation_cycle.cpp
 * @brief HAND-WRITTEN - the body of the `irrigation` POU.
 */

#include <cmath>
#include <cmath>
#include "libbotanik/utils/time.h"

#include "irrigation_cycle.h"

#include "../../../vars.h"

namespace Site {

void IrrigationCycle::on_bind() {
    for (uint8_t i = 0; i < 2; i++) {
        inlet_v[i].attach(inlet[i]);
        outlet_v[i].attach(outlet[i]);
    }
    for (uint8_t i = 0; i < N_TRAY_VALVE; i++) tray_v[i].attach(tray_valve[i]);
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
    const float split_lim = Plc::limit(split.get(), 0.0f, 1.0f);
    const float stretch = powf(mod, 1.0f - split_lim);

    uint8_t  best = 0;
    uint32_t worst_overdue = 0;
    for (uint8_t i = 0; i < N_TRAY_VALVE; i++) {
        if (force_on[i].get()) return (uint8_t)(i + 1);
        if (today == 0) continue;                     // no date, no schedule
        const float summer_days = (float)days[i].get();
        if (summer_days <= 0.0f) continue;        // this tray is not scheduled

        // Damper air stretches the interval; drier air shortens it back toward
        // the summer figure. One day is the floor: a cycle is a day's work.
        const float interval = (stretch > 0.0f) ? (summer_days / stretch)
                                                : summer_days;
        const uint32_t due_after = (interval < 1.0f)
                                       ? 1u : (uint32_t)lroundf(interval);

        const uint32_t served_on = last[i].get();
        // A tray never served has no history to be overdue against, so it goes
        // first; one whose stored day is in the FUTURE is a clock that moved
        // backwards, and waiting for it to catch up would strand the tray.
        const uint32_t elapsed = (served_on == 0 || served_on > today)
                                     ? due_after : (today - served_on);
        if (elapsed < due_after) continue;

        // Held back by hand, and ONCE: the skip is spent on the watering it
        // was due for, and the tray goes to the back of the queue rather than
        // arriving again tomorrow with the flag gone.
        if (force_off[i].get()) {
            _clear_force(force_off[i]);
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
            period.set(interval);
        }
    }
    return best;
}

void IrrigationCycle::_set_valves(IrrState st, uint8_t tray) {
    // Fill: inlet open, this tray open, outlet shut. Drain: inlet shut, tray
    // still open, outlet open. Every other tray is shut in both, so one cycle
    // can never water two.
    const bool filling  = (st == IRR_FILLING);
    const bool draining = (st == IRR_DRAINING);
    // SOAKING keeps the tray connected with both ends of the manifold shut, so
    // the dose sits where it was put and nothing moves.
    const bool soaking  = (st == IRR_SOAKING);
    // FLUSHING opens BOTH ends with no tray between them: fresh water runs the
    // length of the shared manifold and out through the drain, so what sat in
    // the line after a tray emptied is not what the next tray gets first.
    for (uint8_t i = 0; i < 2; i++) {
        inlet_v[i].set(filling || st == IRR_FLUSHING);
        outlet_v[i].set(draining || st == IRR_FLUSHING);
    }
    for (uint8_t i = 0; i < N_TRAY_VALVE; i++)
        tray_v[i].set((filling || soaking || draining) && (i + 1 == tray));
}

void IrrigationCycle::_blame(const Vars::RefOut<bool>* per_tray, bool on) {
    // The tray being served, and only it. The any-form beside these is what an
    // operator sees; this is what still says WHICH tray once the cycle has
    // moved on to the next one.
    if (_tray >= 1 && _tray <= N_TRAY_VALVE) per_tray[_tray - 1].set(on);
}

void IrrigationCycle::_park_valves() {
    for (uint8_t i = 0; i < 2; i++) {
        inlet_v[i].park();
        outlet_v[i].park();
    }
    for (uint8_t i = 0; i < N_TRAY_VALVE; i++) tray_v[i].park();
}

void IrrigationCycle::_stamp(uint8_t tray, uint32_t day) const {
    // An EDIT, not a drive: the row is a PARAMETER, and driving it would write
    // the value while leaving flash owing nothing - so the schedule would
    // restart from nothing at the next boot. Once a cycle is a rate flash is
    // happy with.
    (void)last[tray].edit((uint32_t)day);
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
    _clear_force(force_on[_tray - 1]);
    if (_forced) { _clear_force(force_now); _forced = false; }
    if (_served < 0xFFu) _served++;

    // AFTER THE FILL, not after the drain: the tray has had its water by then,
    // and a fault or a restart during the long drain must not water it twice.
    // A forced cycle stamps too - it IS a watering, and leaving it unrecorded
    // would water the tray again on its own account tomorrow.
    const uint32_t day = today.get();
    if (day != 0) _stamp((uint8_t)(_tray - 1), day);
}

bool IrrigationCycle::_in_slot() const {
    const DayPhase now = (DayPhase)phase.get();

    // END OF NIGHT, and only when the plants are not cold: watering cold roots
    // is what the gate exists to prevent, and the morning slot below is where
    // that watering goes instead.
    if (now == PHASE_COLD_BANK || now == PHASE_DAWN_BREEZE)
        return outside_temp.get() >= dawn_gate.get();

    // START OF THE DAY, for a window an operator sets: the tray has to drain
    // and dry before the stomata open at dusk.
    return now == PHASE_DAY &&
           from_rise.get() <= (int16_t)(day_rise.get() +
                                        (int16_t)morning_win.get());
}

void IrrigationCycle::_drain_verdict() {
    const bool measured = out_flow.valid();
    if (_dose_L <= 0.0f || !measured) {
        // A fill that never delivered, or a meter that is not answering. Both
        // are already alarms of their own; a clog raised on top would send
        // somebody to the wrong end of the plant.
        drain_fault.set(false);
        _blame(drain_tray, false);
        return;
    }
    // Against the dose, not against a fixed volume: a modulated half-dose
    // drains half as much and is not a clog.
    // The published row, not the difference behind it: a verdict made on a
    // number nobody can see is a verdict nobody can rehearse.
    const float frac     = drained.get() / _dose_L;
    const float min_frac = exp_drain.get() - drain_margin.get();
    const bool  clog     = frac < min_frac;
    drain_fault.set(clog);
    _blame(drain_tray, clog);
}

IrrState IrrigationCycle::_after_fill() const {
    // A soak of zero is SKIPPED, not passed through for one activation: a state
    // the field bus sees flicker is a state somebody has to explain.
    return (soak_s.get() > 0) ? IRR_SOAKING : IRR_DRAINING;
}

void IrrigationCycle::_enter(IrrState st) {
    _state = st;
    _in_state_ms = 0;
    // The meters are counters, so a window is a DIFFERENCE: stamp the one the
    // next st is judged on and read it again when the st ends.
    if (st == IRR_FILLING) _fill_start_L = in_total.get();
    if (st == IRR_DRAINING) {
        _drain_start_L = out_total.get();
        drained.set(0.0f);
    }
    _set_valves(st, _tray);
    state.set((uint8_t)st);
    active.set(_tray);
}

void IrrigationCycle::run(ProgramContext& c) {
    if (inhibit_requested()) {
        // Where a dead controller would leave them, which is not the same for
        // all ten: the inlet fails SHUT and the drains fail OPEN, so a tray
        // that was filling empties rather than sitting full.
        _park_valves();
        _tray  = 0;
        _state = IRR_IDLE;
        state.set((uint8_t)IRR_IDLE);
        active.set((uint8_t)0);
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
    const bool  can_dose = mod.valid();
    const float mod_now  = mod.get();

    // The daily tally is against a DATE, not an uptime: it rolls when the day
    // does, so a cycle running through midnight is not counted twice and a
    // reboot at noon does not hand the plant a second allowance.
    //
    // Published and read back, like every other decision this law makes: the
    // date is what the whole schedule turns on, and one nobody can see is one
    // nobody can rehearse.
    today.set(_today());
    const uint32_t day = today.get();
    if (day != 0 && day != _served_day) {
        _served_day = day;
        _served     = 0;
    }
    const float split_lim = Plc::limit(split.get(), 0.0f, 1.0f);
    const float dose = (_tray > 0)
        ? (float)litres[_tray - 1].get() * powf(mod_now, split_lim) : 0.0f;
    if (can_dose) volume.set(dose);
    else          volume.hold();

    switch (_state) {
        case IRR_IDLE: {
            delivered.set(0.0f);
            if (_served >= trays_per_day.get() && !force_now.get())
                break;                         // the surplus waits for tomorrow
            // WHEN, before which tray: the climate decides the slot and the
            // schedule decides the tray, and an operator forcing a cycle
            // overrides both.
            if (!force_now.get() && !_in_slot()) break;
            if (!can_dose) break;

            const uint8_t next = _next_due(mod_now, day);
            if (next == 0) break;
            _tray   = next;
            _forced = force_now.get();
            _enter(IRR_FILLING);
            break;
        }
        case IRR_FILLING: {
            delivered.set(in_total.get() - _fill_start_L);
            // ...and read back, so the fill ends on the row an operator can
            // watch and force rather than on a local difference.
            const float got = delivered.get();
            if (got >= dose && dose > 0.0f) {
                fill_fault.set(false);
                _blame(fill_tray, false);
                _stamp_served();
                _dose_L = got;
                _enter(_after_fill());
            } else if (_in_state_ms >= 1000u * max_fill_s.get()) {
                // The dose never arrived: water off, a stuck inlet, or a meter
                // that has stopped counting. Move to drain regardless - leaving
                // the inlet open on a fault is how a tray overflows.
                fill_fault.set(true);
                _blame(fill_tray, true);
                // What actually went in, which may be nothing: the drain is
                // judged against the water the tray really got.
                _dose_L = got;
                // ...and straight to the drain, soak or no soak. A soak steeps
                // a tray in the dose it was given; whatever is in this one got
                // there by accident, and the slot is better spent on the next
                // tray than on waiting out a fault.
                _enter(IRR_DRAINING);
            }
            break;
        }
        case IRR_SOAKING: {
            // The tray sits full. Nothing is measured here and nothing can go
            // wrong: the fill has already been judged, and the drain window
            // has not started, so the only thing this state does is wait.
            if (_in_state_ms >= 1000u * soak_s.get()) _enter(IRR_DRAINING);
            break;
        }
        case IRR_DRAINING: {
            drained.set(out_total.get() - _drain_start_L);
            if (_in_state_ms < 1000u * drain_window_s.get()) break;
            _drain_verdict();
            _tray = 0;
            _enter(IRR_FLUSHING);
            break;
        }
        case IRR_FLUSHING: {
            if (_in_state_ms >= 1000u * flush_s.get()) _enter(IRR_IDLE);
            break;
        }
    }
    served.set(_served);
}

}  // namespace Site
