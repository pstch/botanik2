/**
 * @file irrigation_modulation.cpp
 * @brief HAND-WRITTEN - the body of the `irr_modulation` POU.
 */

#include <cmath>
#include "libbotanik/utils/time.h"

#include "irrigation_modulation.h"

#include "../../../vars.h"

namespace Site {

const uint64_t IrrigationModulation::FOLD_MS = 3600ull * 1000ull;

float IrrigationModulation::_track_dryness(ProgramContext& c) {
    // Once an HOUR, and the interval is the whole design: folded every scan the
    // increment underflows a float and the average freezes; folded once a day
    // the answer depends on which hour it was taken.
    if (c.now_ms - _last_fold_ms >= FOLD_MS || !_seeded) {
        const uint16_t days = dry_days.get();
        const float alpha = (days == 0) ? 1.0f : 1.0f / (24.0f * (float)days);
        dryness.configure(alpha, alpha);
        const float now = vpd.get();
        if (!_seeded) { dryness.preset(now); _seeded = true; }
        else          { dryness.push(now); }
        _last_fold_ms = c.now_ms;
    }
    vpd_avg.set(dryness.value());
    return dryness.value();
}

void IrrigationModulation::observe(ProgramContext& c) {
    const float vpd_avg = _track_dryness(c);

    const float demand  = Plc::ladder(vpd_avg, dry_lo.get(), dry_hi.get());
    const float min_mod = Plc::limit(mod_min.get(), 0.0f, 1.0f);
    // Gamma is held at 1 or more so the curve is concave-up: watering reaches
    // its summer figure only once dryness is really established, and every
    // error in between is toward LESS water.
    const float gamma = Plc::max_of(1.0f, mod_gamma.get());
    mod.set(Plc::limit(min_mod + (1.0f - min_mod) * powf(demand, gamma),
                       min_mod, 1.0f));
}

}  // namespace Site
