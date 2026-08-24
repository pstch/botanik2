/**
 * @file strat_sensor.cpp
 * @brief HAND-WRITTEN - the body of the `strat` POU.
 */

#include "strat_sensor.h"

namespace Site {

void StratSensor::run(ProgramContext& c) {
    // A gap computed from a level nothing is measuring is not a small error,
    // it is a number about a place. The levels hold their last readings, so
    // the difference is still the last profile this law saw - written either
    // way, with the quality saying nobody measured it. FAULTED, not degraded:
    // the distinction is whether the OUTPUT is still good, and here it is gone.
    const bool measured = ceiling.valid() && floor.valid();
    if (!measured) faulted();

    // ABSOLUTE: destratification mixes the air whichever way the profile has
    // gone, and an inverted one - a cold roof at night over a warm floor - is
    // as much a case for the fans as the usual way round. The sign would only
    // matter to a law that wanted to move heat in one direction, and none does.
    strat.set(Plc::abs_of(Plc::sub(ceiling.get(), floor.get())), measured);
}

}  // namespace Site
