/**
 * @file day_phase_controller.cpp
 * @brief HAND-WRITTEN - the body of the `day_phase` POU.
 */

#include "day_phase_controller.h"

namespace Site {

DayPhase DayPhaseController::_phase_at(int16_t since_rise,
                                       int16_t since_set) const {
    // Down the day in order, so a boundary an operator has trimmed past the one
    // after it shortens its own phase rather than swallowing the next.
    if (since_rise < cold_bank_rise.get()) return PHASE_NIGHT;
    if (since_rise < breeze_rise.get())
        return cold_arm.get() ? PHASE_COLD_BANK : PHASE_NIGHT;
    if (since_rise < day_rise.get())       return PHASE_DAWN_BREEZE;
    if (since_set  < heat_bank_set.get())  return PHASE_DAY;
    if (since_set  < dusk_set.get())
        return heat_arm.get() ? PHASE_HEAT_BANK : PHASE_DAY;
    if (since_set  < night_set.get())      return PHASE_DUSK_BREEZE;
    return PHASE_NIGHT;
}

void DayPhaseController::run(ProgramContext& c) {
    const float t_in = inside.get();
    // What the two halves of this law stand on: the sun says WHERE the day is,
    // the interior probe says what it did. Either can go, and they go
    // separately - a boot with no NTP has the probe and not the sun.
    const bool sun_ok  = from_rise.valid() && from_set.valid();

    if (c.first_scan || !_seeded) {
        _day_max = _night_min = t_in;
        _seeded  = true;
    }
    if (t_in > _day_max)   _day_max = t_in;
    if (t_in < _night_min) _night_min = t_in;

    // The extremes are gathered per phase, not per calendar day: what COLD_BANK
    // needs is how hot it got before it, and the phase boundary is where that
    // window closes.
    const DayPhase was = _phase;

    // Where the day is, as sun_watch published it. Unknown until the tracker
    // has a location and a wall clock - every boot, for as long as NTP takes -
    // and the phase HOLDS through that rather than announcing a night the plant
    // is not in.
    if (sun_ok) _phase = _phase_at(from_rise.get(), from_set.get());

    if (_phase == PHASE_DAY && was != PHASE_DAY)     _day_max   = t_in;
    if (_phase == PHASE_NIGHT && was != PHASE_NIGHT) _night_min = t_in;

    phase.set((uint8_t)_phase, sun_ok);
    // An unplaced phase must apply NO bank offset downstream, and the row
    // declares one that would. The companion is what says so.
    phase_qlt.set(sun_ok, true);
    // The extremes are an accumulation, and a probe that has gone stops adding
    // to it: what is published is the window as it stood when the house was
    // last visible, which is the best answer there is and not a current one.
    prev_day_max.set(_day_max, true);
    prev_night_min.set(_night_min, true);

    // Whether the bank phases have anything to do when their turn comes. Both
    // can be armed at once - a clear late-spring day is hot and its night cold,
    // and the two are decided independently.
    //
    // Off the ROWS just written, not off the members behind them: forcing one
    // is how a cold-bank morning gets rehearsed in July, and a law that armed
    // on its own copy would leave the page saying something it was not doing.
    cold_arm.set(prev_day_max.get() > cold_bank_trg.get(), true);
    heat_arm.set(prev_night_min.get() < heat_bank_trg.get(), true);
}

}  // namespace Site
