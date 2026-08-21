/**
 * @file strat_sensor.cpp
 * @brief HAND-WRITTEN - the body of the `strat` POU.
 */

#include <cmath>

#include "strat_sensor.gen.h"

namespace Site {

void StratSensor::run(ProgramContext& c) {
    // A gap computed from a level nothing is measuring is not a small error,
    // it is a number about a place. Say so instead - and FAULTED, not degraded:
    // the distinction is whether the OUTPUT is still good, and here it is gone.
    if (!ceiling.valid() || !floor.valid()) {
        // The levels hold their last readings, so their difference is still the
        // last profile this law saw. Held, not dropped: the fans read the
        // quality and go to their own floor, and a page reads a gap the plant
        // actually had.
        strat.hold(fabsf(ceiling.get() - floor.get()));
        faulted();
        return;
    }
    // ABSOLUTE: destratification mixes the air whichever way the profile has
    // gone, and an inverted one - a cold roof at night over a warm floor - is
    // as much a case for the fans as the usual way round. The sign would only
    // matter to a law that wanted to move heat in one direction, and none does.
    strat.set(fabsf(ceiling.get() - floor.get()));
}

}  // namespace Site
