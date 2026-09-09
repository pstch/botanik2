#ifndef REF_SIM_H
#define REF_SIM_H

/**
 * @file sim.h
 * @brief HAND-WRITTEN - the ref simulated field: the data animator the
 *        simulator runs.
 *
 * Not generated: how a plant BEHAVES is judgement, not schema. It animates
 * this site's own rows, so the simulator exercises the layout the hardware
 * will.
 *
 * Header-only, and the whole body is guarded by TARGET_SIMULATOR: site code
 * must cross-compile for the OPTA, where the real field plays this role.
 */

#ifdef TARGET_SIMULATOR

#include <cstdint>

#include "libbotanik.h"

namespace Site {

/** @brief How the harness reaches the simulated field bus.
 *
 *  A reading on the bus goes into the DEVICE, not into the row: the row should
 *  show what the wire delivered, at the rate it delivered it. The simulator
 *  fills these in at bring-up; the site only declares them, so it still
 *  cross-compiles for the OPTA where the real field plays this role. */
typedef void (*FieldSink)(void* ctx, Botanik::Vars::Var h, float v);
inline FieldSink& field_sink() { static FieldSink fn = nullptr; return fn; }
inline void*&     field_ctx()  { static void* ctx = nullptr; return ctx; }

/** @brief Take a device off the wire, or put it back. A device left holding
 *  its last register answers every poll and the row stays valid, so silence is
 *  the only way to simulate one that died. */
typedef void (*FieldFault)(void* ctx, uint8_t unit, bool absent);
inline FieldFault& field_fault() { static FieldFault fn = nullptr; return fn; }

/** @brief Publish through the bus where the row is on one, into the row where
 *  it is not. */
inline void put(Botanik::Vars::Bank& io, uint16_t row, float v) {
    Botanik::Vars::Var h = io.var(row);
    if (h.meta().bind.device != 0 && field_sink() != nullptr)
        field_sink()(field_ctx(), h, v);
    else
        h.set(v);
}

/** @brief One day of air temperature, and a vent that follows its command.
 *
 *  The two levels differ by a fixed stratification, so the law's "warmer of
 *  the two" has something to pick. Deterministic: a sine of the hour, with no
 *  noise, because a reader comparing two runs should see the same numbers.
 */
inline void simulate(Botanik::Vars::Bank& io, uint32_t now_ms) {
    using namespace Botanik::Vars;

    // Hour of a 24-hour day, from the virtual clock.
    const float hour = (float)((now_ms / 1000u) % 86400u) / 3600.0f;
    // Coldest before dawn, warmest mid-afternoon.
    const float base = 18.0f + 9.0f * (float)(-__builtin_cos((hour - 3.0f)
                                              * 3.14159265f / 12.0f));

    put(io, Row::air_temp_hi1, base);
    put(io, Row::air_temp_hi2, base - 1.5f);   // the cooler level

    // The actuator reaches its command in one beat: this site has no travel
    // time to model, and a vent that lies about where it is would be a
    // different example.
    put(io, Row::vent_drive, io.var(Row::vent_pos).as_f32());
}

}  // namespace Site

#endif // TARGET_SIMULATOR

#endif // REF_SIM_H
