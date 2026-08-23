/**
 * @file climate_watch.cpp
 * @brief HAND-WRITTEN - the body of the `climate_watch` POU.
 */

#include "climate_watch.gen.h"

namespace Site {

void ClimateWatch::run(ProgramContext& c) { (void)c; }

void ClimateWatch::observe(ProgramContext& c) {
    // Rolled up from the four zones: "a sensor is faulty" is a fact about the
    // plant that a supervisor wants without knowing there are four of them.
    bool blind = false;
    for (const Vars::RefIn<bool>& a : sns) blind = blind || c.bank.get(a);
    sensor.set(blind);

    const bool measured = temp.valid() &&
                          margin.valid();
    if (!measured) {
        // Nothing is raised on a reading nobody has. The quorum alarm above is
        // what says the plant cannot be seen, and it is already raised.
        degraded();
        hot.set(false);
        cold.set(false);
        humid.set(false);
    } else {
        hot.set(temp.get() > too_hot.get());
        cold.set(temp.get() < too_cold.get());
        // A MARGIN, not an RH: air at 90% and 5 degC is drier than air at 60%
        // and 25, and the alarm has to mean one thing at both. The line is the
        // dehumidify arm's own saturation point, so the alarm cannot disagree
        // with the actuator it is about.
        humid.set(margin.get() < humid_at.get());
    }

    // Past the band the fans are already flat out on: the gap is not something
    // destratification is going to close on its own.
    stratified.set(strat.valid() &&
                 strat.get() > strat_hi.get());
}

}  // namespace Site
