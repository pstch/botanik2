/**
 * @file temp_rh_zone.cpp
 * @brief HAND-WRITTEN - the body of the `temp_rh_zone` POU.
 */

#include "temp_rh_zone.gen.h"

namespace Site {

namespace {
    /** @brief A derived row, written every beat. @p seen is whether any level
     *  measured what it came from; if none did the value stands, marked not
     *  current. */
    void publish(Vars::RefOut<float> row, float v, bool seen) {
        if (seen) row.set(v);
        else      row.hold(v);
    }
}  // namespace

void TempRHZone::on_bind() {
    const float tau_s = 0.001f * (float)tau_ms.get();
    ceiling.attach(t_hi, h_hi, temp_hi, rh_hi, fault_hi, tau_s);
    floor.attach(t_lo, h_lo, temp_lo, rh_lo, fault_lo, tau_s);
    crawlspace.attach(t_cs, h_cs, temp_cs, rh_cs, fault_cs, tau_s);
    exterior.attach(t_ext, h_ext, temp_ext, rh_ext, fault_ext, tau_s);
}

void TempRHZone::run(ProgramContext& c) {
    if (c.first_scan) {
        // The filters hold a claim about the recent past, and not being called
        // means we stopped watching. Seed them again rather than resuming an
        // average of readings from before the outage.
        ceiling.reset(); floor.reset();
        crawlspace.reset(); exterior.reset();
    }

    ceiling.step(c.dt_ms);
    floor.step(c.dt_ms);
    crawlspace.step(c.dt_ms);
    exterior.step(c.dt_ms);

    const float t_hi = ceiling.temperature();
    const float t_lo = floor.temperature();

    // ONE interior temperature, for everything that wants the house rather than
    // a level of it - the cooling ladder included, which is why it is the
    // HOTTER of the two and not their mean. The spec keeps the worst case
    // between levels, and for heat that is the top of the house: an average
    // would hide a ridge already past the target under a cool floor.
    const bool  hi_ok  = ceiling.valid();
    const bool  lo_ok  = floor.valid();
    const float hotter = Plc::max_of(t_hi, t_lo);
    if (hi_ok && lo_ok)  temp_in.set(hotter);
    else if (hi_ok)      temp_in.set(t_hi);
    else if (lo_ok)      temp_in.set(t_lo);
    // Both levels gone: the zones are holding their last readings, so the
    // hotter of them is still the best answer there is - held, like they are.
    else                 temp_in.hold(hotter);

    // Between the two interior levels the WORST case is kept, per the spec:
    // the smallest margin decides the condensation risk, and taking an average
    // would hide a level that is already at the dew point.
    // One dew point per level: the margin and the VPD are both derived from
    // it, and computing it three times over cost three logarithms a beat.
    const float dp_hi = Plc::dew_point(t_hi, ceiling.humidity());
    const float dp_lo = Plc::dew_point(t_lo, floor.humidity());
    const float m_hi = t_hi - dp_hi;
    const float m_lo = t_lo - dp_lo;
    // Among the levels that are MEASURED, like temp_in above: one holding a
    // reading from before its probes died would otherwise win the worst-case
    // vote and speak for the house.
    const bool  worst_isceiling = (hi_ok == lo_ok) ? (m_hi <= m_lo) : hi_ok;
    const float t_worst  = worst_isceiling ? t_hi : t_lo;

    // Published every beat whatever the zones did, and the QUALITY is what
    // says nothing measured it - a reader that checks learns not to act, and
    // one that does not gets the margin the house last really had rather than
    // a zero, which reads like saturated air.
    const bool seen = hi_ok || lo_ok;
    publish(dew_point, worst_isceiling ? dp_hi : dp_lo, seen);
    publish(dp_margin, worst_isceiling ? m_hi : m_lo, seen);
    publish(sat_press, Plc::sat_vapour_pressure(t_worst), seen);

    // The worst case is per QUANTITY, not one level chosen for all of them: the
    // spec keeps the smallest dew-point margin for rot and the LOWEST VPD for
    // irrigation, and they are not always the same level. A margin and a VPD
    // tell the same story in different units, and the units are not linear in
    // each other - the same margin is a much larger VPD when it is warm.
    const float vpd_hi = Plc::sat_vapour_pressure(t_hi) -
                         Plc::sat_vapour_pressure(dp_hi);
    const float vpd_lo = Plc::sat_vapour_pressure(t_lo) -
                         Plc::sat_vapour_pressure(dp_lo);
    publish(vpd, (hi_ok == lo_ok) ? Plc::min_of(vpd_hi, vpd_lo)
                                  : (hi_ok ? vpd_hi : vpd_lo), seen);

    const float limit = rot_limit.get();
    rot.set((worst_isceiling ? m_hi : m_lo) < limit);
}

}  // namespace Site
