/**
 * @file day_phase_controller.cpp
 * @brief HAND-WRITTEN - the body of the `day_phase` POU.
 */

#include "day_phase_controller.gen.h"

namespace Site {

DayPhase DayPhaseController::_phase_at(int16_t from_rise,
                                       int16_t from_set) const {
    // Down the day in order, so a boundary an operator has trimmed past the one
    // after it shortens its own phase rather than swallowing the next.
    if (from_rise < cold_bank_rise.get()) return PHASE_NIGHT;
    if (from_rise < breeze_rise.get())
        return cold_arm.get() ? PHASE_COLD_BANK : PHASE_NIGHT;
    if (from_rise < day_rise.get())       return PHASE_DAWN_BREEZE;
    if (from_set  < heat_bank_set.get())  return PHASE_DAY;
    if (from_set  < dusk_set.get())
        return heat_arm.get() ? PHASE_HEAT_BANK : PHASE_DAY;
    if (from_set  < night_set.get())      return PHASE_DUSK_BREEZE;
    return PHASE_NIGHT;
}

void DayPhaseController::run(ProgramContext& c) {
    const float t_in = inside.get();

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
    // is not in. (The base has already reported this program degraded: the
    // offsets are declared inputs.)
    if (from_rise.valid() && from_set.valid())
        _phase = _phase_at(from_rise.get(), from_set.get());

    if (_phase == PHASE_DAY && was != PHASE_DAY)     _day_max   = t_in;
    if (_phase == PHASE_NIGHT && was != PHASE_NIGHT) _night_min = t_in;

    phase.set((uint8_t)_phase);
    prev_day_max.set(_day_max);
    prev_night_min.set(_night_min);

    // Whether the bank phases have anything to do when their turn comes. Both
    // can be armed at once - a clear late-spring day is hot and its night cold,
    // and the two are decided independently.
    //
    // Off the ROWS just written, not off the members behind them: forcing one
    // is how a cold-bank morning gets rehearsed in July, and a law that armed
    // on its own copy would leave the page saying something it was not doing.
    cold_arm.set(prev_day_max.get() > cold_bank_trg.get());
    heat_arm.set(prev_night_min.get() < heat_bank_trg.get());
}

}  // namespace Site
