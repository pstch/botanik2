/**
 * @file temp_rh_zone.cpp
 * @brief HAND-WRITTEN - the body of the `temp_rh_zone` POU.
 */

#include "temp_rh_zone.h"

namespace Site {

void TempRHZone::run(ProgramContext& c) {
    (void)c;
    const float t_hi = temp_hi.get();
    const float t_lo = temp_lo.get();

    // ONE interior temperature, for everything that wants the house rather than
    // a level of it - the cooling ladder included, which is why it is the
    // HOTTER of the two and not their mean. The spec keeps the worst case
    // between levels, and for heat that is the top of the house: an average
    // would hide a ridge already past the target under a cool floor.
    const bool  hi_ok  = temp_hi.valid();
    const bool  lo_ok  = temp_lo.valid();
    // Every write below carries its own quality, so the law degrades rather
    // than stopping - but a reader asking why the house has one temperature
    // instead of two asks the program, not the rows.
    const float hotter = Plc::max_of(t_hi, t_lo);
    if (hi_ok && lo_ok)  temp_in.set(hotter, true);
    else if (hi_ok)      temp_in.set(t_hi, true);
    else if (lo_ok)      temp_in.set(t_lo, true);
    // Both levels gone: the rows are holding their last readings, so the
    // hotter of them is still the best answer there is - held, like they are.
    else                 temp_in.set(hotter, false);

    // Between the two interior levels the WORST case is kept, per the spec:
    // the smallest margin decides the condensation risk, and taking an average
    // would hide a level that is already at the dew point.
    // One dew point per level: the margin and the VPD are both derived from
    // it, and computing it three times over cost three logarithms a beat.
    const float dp_hi = Plc::dew_point(t_hi, rh_hi.get());
    const float dp_lo = Plc::dew_point(t_lo, rh_lo.get());
    const float m_hi = t_hi - dp_hi;
    const float m_lo = t_lo - dp_lo;
    // Among the levels that are MEASURED, like temp_in above: one holding a
    // reading from before its probes died would otherwise win the worst-case
    // vote and speak for the house.
    const bool  worst_isceiling = (hi_ok == lo_ok) ? (m_hi <= m_lo) : hi_ok;
    const float t_worst  = worst_isceiling ? t_hi : t_lo;

    // Published every beat whatever the sensors did, and the QUALITY is what
    // says nothing measured it - a reader that checks learns not to act, and
    // one that does not gets the margin the house last really had rather than
    // a zero, which reads like saturated air.
    const bool seen = hi_ok || lo_ok;
    dew_point.set(worst_isceiling ? dp_hi : dp_lo, seen);
    dp_margin.set(worst_isceiling ? m_hi : m_lo, seen);
    sat_press.set(Plc::sat_vapour_pressure(t_worst), seen);

    // The worst case is per QUANTITY, not one level chosen for all of them: the
    // spec keeps the smallest dew-point margin for rot and the LOWEST VPD for
    // irrigation, and they are not always the same level. A margin and a VPD
    // tell the same story in different units, and the units are not linear in
    // each other - the same margin is a much larger VPD when it is warm.
    const float vpd_hi = Plc::sat_vapour_pressure(t_hi) -
                         Plc::sat_vapour_pressure(dp_hi);
    const float vpd_lo = Plc::sat_vapour_pressure(t_lo) -
                         Plc::sat_vapour_pressure(dp_lo);
    vpd.set((hi_ok == lo_ok) ? Plc::min_of(vpd_hi, vpd_lo)
                             : (hi_ok ? vpd_hi : vpd_lo), seen);

    const float limit = rot_limit.get();
    rot.set((worst_isceiling ? m_hi : m_lo) < limit, true);
}

}  // namespace Site
