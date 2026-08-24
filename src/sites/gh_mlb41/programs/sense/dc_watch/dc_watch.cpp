/**
 * @file dc_watch.cpp
 * @brief HAND-WRITTEN - the body of the `dc_watch` POU.
 */

#include "dc_watch.h"

#include "../../../blocks/ac_supply/ac_supply.h"
namespace Site {

struct Rail {
    Plc::Ewma&           avg;
    Plc::Deadband&       band;
    Vars::RefIn<float>   in;
    Vars::RefOut<float>  out;
    Vars::RefIn<float>   lo;
    Vars::RefIn<uint16_t> avg_ms;
};

void DcWatch::run(ProgramContext& c) { (void)c; }

static bool _rail(const Rail& r, uint32_t dt_ms, bool& seen) {
    // Averaged, then deadbanded: the first says what the rail is doing, the
    // second says when that is worth a write.
    //
    // Only a MEASURED sample is folded in. The row reads its default while the
    // bus is still coming up, and folding that seeds the average at zero: the
    // rail then reads under its floor for as long as the filter takes to climb
    // out of it, which is a supply alarm on every boot.
    const bool  measured = r.in.valid();
    const float tau_s = 0.001f * (float)r.avg_ms.get();
    const float volts = measured ? r.avg(r.in.get(), tau_s, dt_ms)
                                 : r.avg.value();
    // A rail nobody is measuring has no voltage to publish. Its LAST average
    // stays in the filter, so a divider that comes back rejoins where it left.
    if (measured) r.out.set(r.band(volts, SUPPLY_V_BAND));
    else          r.out.invalidate();
    if (measured) seen = true;
    // A divider that has never answered is the bus still coming up. One that
    // has, and has stopped, is a rail nobody can see - which is a fault.
    if (!measured) return !seen;
    return volts >= r.lo.get();
}

void DcWatch::observe(ProgramContext& c) {
    if (c.first_scan) {
        ps1_mean.reset(); ps1_band.reset();
        ps2_mean.reset(); ps2_band.reset();
        ups_mean.reset(); ups_band.reset();
        gen_mean.reset(); gen_band.reset();
        low_for.reset();
    }

    const Rail rails[] = {
        { ps1_mean, ps1_band, dc1,     out_dc1, dc1_lo, dc1_avg },
        { ps2_mean, ps2_band, dc2,     out_dc2, dc2_lo, dc2_avg },
        { ups_mean, ups_band, ups_bat, out_ups, ups_lo, ups_avg },
        { gen_mean, gen_band, gen_bat, out_gen, gen_lo, gen_avg },
    };
    const bool ps1 = _rail(rails[0], c.dt_ms, _seen[0]);
    const bool ps2 = _rail(rails[1], c.dt_ms, _seen[1]);
    const bool ups = _rail(rails[2], c.dt_ms, _seen[2]);
    const bool gen = _rail(rails[3], c.dt_ms, _seen[3]);

    ps1_flt.set(!ps1);
    ps2_flt.set(!ps2);
    gen_flt.set(!gen);

    // THE CONTACT, not the divider: the UPS knows what its own battery has
    // left, and a voltage on the rail is a guess at it. Debounced, because a
    // dry contact bounces and this one holds the whole plant.
    const bool low = low_for(low_bat.get(), 2000, c.dt_ms);
    ups_flt.set(low);
    lockout.set(low);

    // ...and the lockout IS the plant's safe state: every program is asked to
    // park while it stands, and released the moment it does not. Held rather
    // than requested, so it cannot cancel a halt that is still being waited on.
    if (_tasks != nullptr && low != _safe_held) {
        _safe_held = low;
        _tasks->hold_safe(low);
    }

    // The battery voltage is a WARNING and the contact is the interlock, so the
    // summary carries both: an operator reading one row wants the whole answer.
    ok.set(ps1 && ps2 && ups && gen && !low);
}

}  // namespace Site
