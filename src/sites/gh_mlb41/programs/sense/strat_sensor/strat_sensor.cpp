/**
 * @file strat_sensor.cpp
 * @brief HAND-WRITTEN - the body of the `strat` POU.
 */

#include "strat_sensor.h"

namespace Site {

void StratSensor::run(ProgramContext& c) {
    (void)c;

    // A gap computed from a level nothing is measuring is not a small error,
    // it is a number about a place. The levels hold their last readings, so
    // the difference is still the last profile this law saw - written either
    // way, with the quality saying nobody measured it. Both levels are watched
    // inputs, so the base has already said DEGRADED by the time this runs.
    const bool measured = ceiling.valid() && floor.valid();

    // ABSOLUTE: destratification mixes the air whichever way the profile has
    // gone, and an inverted one - a cold roof at night over a warm floor - is
    // as much a case for the fans as the usual way round. The sign would only
    // matter to a law that wanted to move heat in one direction, and none does.
    strat.set(Plc::abs_of(Plc::sub(ceiling.get(), floor.get())), measured);
}

}  // namespace Site
