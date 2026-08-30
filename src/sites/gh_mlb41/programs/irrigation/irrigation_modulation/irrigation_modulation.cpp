/**
 * @file irrigation_modulation.cpp
 * @brief HAND-WRITTEN - the body of the `irr_modulation` POU.
 */

#include <cmath>
#include "libbotanik/utils/time.h"

#include "irrigation_modulation.h"

namespace Site {

void IrrigationModulation::run(ProgramContext& c) {
    const float avg = _track_dryness(c);

    const float demand  = Plc::ladder(avg, dry_lo.get(), dry_hi.get());
    const float min_mod = Plc::limit(mod_min.get(), 0.0f, 1.0f);
    // Gamma is held at 1 or more so the curve is concave-up: watering reaches
    // its summer figure only once dryness is really established, and every
    // error in between is toward LESS water.
    const float gamma = Plc::max_of(1.0f, mod_gamma.get());
    // A modulation nobody can read is no dose, and irr_schedule reads it that
    // way: before the first fold there is nothing to modulate BY.
    if (!_seeded) { mod.invalidate(); return; }
    mod.set(Plc::limit(min_mod + (1.0f - min_mod) * powf(demand, gamma),
                       min_mod, 1.0f), vpd_qlt.get());
    mod_qlt.set(vpd_qlt.get(), true);
}

static const uint32_t FOLD_MS = 3600u * 1000u;

float IrrigationModulation::_track_dryness(ProgramContext& c) {
    const bool measured = vpd_qlt.get();

    // Once an HOUR, and the interval is the whole design: folded every scan the
    // increment underflows a float and the average freezes; folded once a day
    // the answer depends on which hour it was taken.
    //
    // The timer is TICKED either way and only RESET on a fold, so an hour that
    // came due blind folds the moment the probes answer instead of waiting out
    // another one.
    const bool due = fold(true, FOLD_MS, c.dt_ms) || !_seeded;
    if (due && measured) {
        const uint16_t days = dry_days.get();
        const float alpha = (days == 0) ? 1.0f : 1.0f / (24.0f * (float)days);
        dryness.configure(alpha, alpha);
        const float now = vpd.get();
        if (!_seeded) { dryness.preset(now); _seeded = true; }
        else          { dryness.push(now); }
        fold.reset();
    }

    // Only a MEASURED sample is folded. The row KEEPS its last VPD while the
    // probes are out, so folding it is the same number an hour apart - and a
    // multi-day average pulled toward whatever the house was doing when it was
    // last visible is worse than one that stopped moving, because it looks
    // like weather.
    if (_seeded) vpd_avg.set(dryness.value(), measured);
    // Nothing has ever been folded, so there is no average - not an average of
    // zero, which is the VPD of saturated air and would read as a wet week.
    else         vpd_avg.invalidate();
    return dryness.value();
}


}  // namespace Site
