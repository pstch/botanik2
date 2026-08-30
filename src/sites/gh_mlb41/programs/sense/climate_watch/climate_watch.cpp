/**
 * @file climate_watch.cpp
 * @brief HAND-WRITTEN - the body of the `climate_watch` POU.
 */

#include "climate_watch.h"

namespace Site {

void ClimateWatch::run(ProgramContext& c) {
    // Rolled up from the four zones: "a sensor is faulty" is a fact about the
    // plant that a supervisor wants without knowing there are four of them.
    bool blind = false;
    for (const Vars::RefIn<bool>& a : sns) blind = blind || c.bank.get(a);
    sensor.set(blind, true);

    const bool measured = temp_qlt.get() && margin_qlt.get();
    if (!measured) {
        // Nothing is raised on a reading nobody has. The quorum alarm above is
        // what says the plant cannot be seen, and it is already raised.
        hot.set(false, true);
        cold.set(false, true);
        humid.set(false, true);
    } else {
        hot.set(temp.get() > too_hot.get(), true);
        cold.set(temp.get() < too_cold.get(), true);
        // A MARGIN, not an RH: air at 90% and 5 degC is drier than air at 60%
        // and 25, and the alarm has to mean one thing at both. The line is the
        // dehumidify arm's own saturation point, so the alarm cannot disagree
        // with the actuator it is about.
        humid.set(margin.get() < humid_at.get(), true);
    }

    // Past the band the fans are already flat out on: the gap is not something
    // destratification is going to close on its own.
    // The gap declares 2.0 degC, under cli_dTStratHi, so an unreadable one
    // does not raise this on its own.
    stratified.set(strat.get() > strat_hi.get(), true);
}


}  // namespace Site
