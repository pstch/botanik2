/**
 * @file skirt_controller.cpp
 * @brief HAND-WRITTEN - the body of the `skirt` POU.
 */

#include "skirt_controller.h"

namespace Site {

void SkirtController::on_bind() { pulse.attach(valve); }

void SkirtController::run(ProgramContext& c) {
    // Wetting air that is already condensing is the one thing this valve must
    // never do, so the lockout is resolved before any demand - and before the
    // pulse phase, which starts again from shut.
    if (inhibit_requested() || rot_lock.get()) {
        pulse.park();
        pulse.reset();
        duty.set(0.0f);
        return;
    }

    // PROPORTIONAL on the humidify arm: the skirt IS the humidifier, and the
    // ladder's effort is the on-time. The dehumidify arm is the intractors'.
    const float humid = Plc::limit(humid_dem.get(), 0.0f, 1.0f);

    // STAGED on the cooling axis, and gated on the air being dry enough for a
    // wet pad to cool at all - in damp air it only adds water. An unknown
    // demand contributes this valve's own safe state, which is SHUT.
    if (!cool_dem.valid()) degraded();
    const float cool = (cool_dem.valid() && dp_margin.get() >= dry_min.get())
                           ? Plc::ladder(cool_dem.get(), cool_start.get(), 1.0f)
                           : 0.0f;

    const float on_time = pulse.modulate((humid > cool) ? humid : cool,
                                         1000u * period_s.get(),
                                         1000u * min_on_s.get(), c.dt_ms);
    duty.set(on_time);
}

}  // namespace Site
