/**
 * @file strat_sensor.cpp
 * @brief HAND-WRITTEN - the body of the `strat` POU.
 */

#include "strat_sensor.h"

namespace Site {

void StratSensor::run(ProgramContext& c) {
    (void)c;

    // ABSOLUTE: destratification mixes the air whichever way the profile has
    // gone, and an inverted one - a cold roof at night over a warm floor - is
    // as much a case for the fans as the usual way round. The sign would only
    // matter to a law that wanted to move heat in one direction, and none does.
    // A gap computed from a level nothing is measuring is a number about a
    // place. Written either way, and the quality is what says so: the row
    // declares the LOW rung, so a reader substituting it keeps the fans
    // mixing without claiming a profile nobody saw.
    strat.set(Plc::abs_of(Plc::sub(ceiling.get(), floor.get())),
              hi_qlt.get() && lo_qlt.get());
}

}  // namespace Site
