/**
 * @file zone_probes.cpp
 * @brief HAND-WRITTEN - the zone block, out of line.
 */

#include "zone_probes.h"

namespace Site {

ZoneVote ZoneProbes::_select(const Vars::RefIn<float>* refs) {
    float    v[3];
    uint8_t  n = 0;
    for (uint8_t i = 0; i < 3; i++) {
        // An invalid probe is EXCLUDED rather than voted on. It reads its
        // declared default, which is outside the band and would drag an average
        // even when the outlier rule dropped it.
        if (!refs[i].valid()) continue;
        v[n++] = refs[i].get();
    }
    switch (n) {
        // THE MEDIAN, per the spec: keep the middle reading and discard both
        // extremes, so a probe stuck high or stuck low can never be the one
        // retained. (The mean of the two closest would be quieter by root-two
        // and reject the same stuck probe; the spec asks for the median, and
        // the spec is what the plant was designed against.)
        case 3:  return ZoneVote{ Plc::median3(v[0], v[1], v[2]), 3 };
        // Two: no middle to keep, and their mean is the only answer there is.
        case 2:  return ZoneVote{ 0.5f * (v[0] + v[1]), 2 };
        // One: believed, because a reading from one probe still beats none.
        case 1:  return ZoneVote{ v[0], 1 };
        default: return ZoneVote{ 0.0f, 0 };
    }
}

bool ZoneProbes::_publish(Vars::RefOut<float> row, Plc::Ewma& filter,
                          const ZoneVote& v, bool& seeded, float tau_s,
                          uint32_t dt_ms, float lo, float hi) {
    if (v.used == 0) {
        // Nothing believable left. The row keeps the last average this zone
        // could make and says it is not current: a reader that checks quality
        // knows not to act on it, and one that does not gets the temperature
        // the greenhouse last had rather than 0 degC, which reads like a
        // measurement of a freezing house. The filter keeps its state and
        // seeding is not re-armed, so a probe coming back rejoins the average
        // it left.
        if (seeded) row.set(Plc::limit(filter.value(), lo, hi), false);
        else        row.invalidate();     // nothing has ever been measured here
        return false;
    }
    seeded = true;                      // the block starts AT its first reading
    row.set(Plc::limit(filter(v.value, tau_s, dt_ms), lo, hi), true);
    return true;
}

uint8_t ZoneProbes::operator()(uint32_t dt_ms) {
    // Each quantity carries its own quorum. Coupling them would let one dead
    // humidity probe invalidate a temperature three good probes agree on.
    const ZoneVote t = _select(_r.t);
    ZoneVote h = _select(_r.h);

    // The instruments report humidity in PERCENT and everything above this line
    // works in fractions - the spec's convention for a percentage is a real
    // number between 0 and 1. Converted HERE, once, at the boundary between the
    // instrument and the model: published as a percent it clamped at 1.00 and
    // every dew-point margin in the plant read zero.
    h.value *= 0.01f;

    _temp_ok = _publish(_r.temp, _temp, t, _temp_seeded, _tau_s, dt_ms,
                        -100.0f, 200.0f);
    _publish(_r.rh, _rh, h, _rh_seeded, _tau_s, dt_ms, 0.0f, 1.0f);

    // Two probes still select correctly, so the plant keeps running and the
    // operator is told to replace one before the second goes. The alarm is the
    // zone's, so it follows whichever quantity has lost the most.
    const uint8_t worst = (t.used < h.used) ? t.used : h.used;
    _r.fault.set(_quorum_for(worst < 3, 5000, dt_ms), true);
    return worst;
}

}  // namespace Site
