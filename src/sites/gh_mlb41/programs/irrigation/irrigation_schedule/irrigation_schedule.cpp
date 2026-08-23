/**
 * @file irrigation_schedule.cpp
 * @brief HAND-WRITTEN - the body of the `irr_schedule` POU.
 */

#include <cmath>

#include "irrigation_schedule.gen.h"

namespace Site {

static uint32_t _today() {
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

static void _clear_force(Vars::RefInOut<bool> r) {
    (void)r.edit(false);
}

void IrrigationSchedule::_stamp(uint8_t tray, uint32_t day) const {
    // An EDIT, not a drive: the row is a PARAMETER, and driving it would write
    // the value while leaving flash owing nothing - so the schedule would
    // restart from nothing at the next boot.
    (void)last[tray].edit((uint32_t)day);
}

bool IrrigationSchedule::_in_slot() const {
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

uint8_t IrrigationSchedule::_next_due(float mod, uint32_t today) {
    // period = summer period / mod^(1 - split), volume = summer volume *
    // mod^split, so their RATIO - the water actually delivered per day - is
    // proportional to mod whatever the split is set to.
    const float split_lim = Plc::limit(split.get(), 0.0f, 1.0f);
    const float stretch = powf(mod, 1.0f - split_lim);

    uint8_t  best = 0;
    uint32_t worst_overdue = 0;
    for (uint8_t i = 0; i < N_DAYS; i++) {
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
            // the PARAMS page and this is what the weather made of it. Written
            // HERE and not after the loop, because a force_on returns early and
            // the row would then never be written at all.
            period.set(interval);
        }
    }
    return best;
}

void IrrigationSchedule::run(ProgramContext& c) {
    (void)c;
    // The daily tally is against a DATE, not an uptime: it rolls when the day
    // does, so a cycle running through midnight is not counted twice and a
    // reboot at noon does not hand the plant a second allowance.
    today.set(_today());
    const uint32_t day = today.get();
    if (day != 0 && day != _served_day) {
        _served_day = day;
        (void)served.set((uint8_t)0);
    }

    const bool forced = force_now.get();
    const bool open   = _in_slot();
    slot_open.set(open);

    // The interval is written only where one is actually chosen, and a forced
    // tray, an empty search and the gate below all skip that. Held here so the
    // row still says what it last was, and says it is not current.
    period.hold();

    // WHEN, before which tray: the climate decides the slot and the schedule
    // decides the tray, and an operator forcing a cycle overrides both. A
    // modulation nobody can read is no dose, so there is nothing to start.
    if ((served.get() >= trays_per_day.get() && !forced) ||
        (!forced && !open) || !mod.valid()) {
        due_tray.set((uint8_t)0);
        return;
    }
    due_tray.set(_next_due(mod.get(), day));
}

}  // namespace Site
