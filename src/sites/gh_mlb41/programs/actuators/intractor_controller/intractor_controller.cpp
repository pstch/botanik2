/**
 * @file intractor_controller.cpp
 * @brief HAND-WRITTEN - the body of the `intractors` POU.
 */

#include "intractor_controller.gen.h"

namespace Site {

float IntractorController::_safe_speed() const {
    // The AO row's declared safe value, not a constant here: an actuator's safe
    // state belongs to the table, which is also what the field bus carries out
    // when control authority goes entirely.
    const Vars::Descriptor& d = intr[0].io->descriptor(intr[0].r.index);
    return (float)Vars::as_f32(d.type, d.def);
}

void IntractorController::run(ProgramContext& c) {
    (void)c;
    // The FLOOR, and it is a SPEED rather than a demand: some air is always
    // wanted, and the row is where this actuator's safe state is declared. A
    // demand below it simply does not reach it.
    float speed = _safe_speed();

    if (!inhibit_requested()) {
        const bool  dem_ok = cool_dem.valid();
        const float cool    = cool_dem.get();
        if (!dem_ok) degraded();

        // Three demands, each gated by the law that owns the reading behind it,
        // and a demand nobody can read contributes NOTHING - it does not have
        // to, since the floor below is already this actuator's safe state.
        //
        //  - cooling, while the intake really is cooler
        //  - the anti-rot ventilation, ungated: rot is the collection, and the
        //    dew point it fights is the one inside
        //  - the DEHUMIDIFY arm alone, while the air outside is drier. Positive
        //    is asking for the skirt, and more air would work against it
        const float cooling = (dem_ok && cool_ok.get()) ? cool : 0.0f;
        const float venting = rot_vent.get();
        const float drying  = dry_ok.get() ? -humid_dem.get() : 0.0f;
        // ...and the OTHER arm too. Too little humidity is answered by the
        // skirt and these fans TOGETHER: the air they draw comes in through the
        // wetted textile, which is the only humidifier the plant has. Ungated -
        // the point is to bring moisture in, so "is it drier outside" is the
        // wrong question for this arm.
        const float wetting = Plc::max_of(0.0f, humid_dem.get());

        const float demand = Plc::max_of(Plc::max_of(cooling, venting),
                                         Plc::max_of(drying, wetting));
        speed = Plc::max_of(speed, Plc::ladder(demand, start_dem.get(),
                                               full_dem.get()));

        // Ground heat is ONE RUNG, not a ramp: the spec's negative half asks
        // for low speed at -0.1 and says nothing about wanting more of it the
        // colder it gets. Pulling harder on a cold house moves more air through
        // the same ground, cooling the ground rather than warming the house.
        if (ground_ok.get())
            speed = Plc::max_of(speed, ground_speed.get());
    }

    for (uint8_t i = 0; i < N_INTR; i++) intr[i].set(speed);
}

}  // namespace Site
