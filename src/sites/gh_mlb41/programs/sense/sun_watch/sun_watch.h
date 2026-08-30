/**
 * @file sun_watch.h
 * @brief The declaration half of the `sun_watch` POU: its rows, its
 *        blocks, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_SUN_WATCH_H
#define SITE_PROGRAM_SUN_WATCH_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

namespace Site {

using namespace Botanik;

/**
 * @brief Where the sun is, published as rows.
 */
class SunWatch final : public Plc::Program {

public:

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<int16_t> from_rise;
    Vars::RefOut<int16_t> from_set;
    Vars::RefOut<float>   elevation;

    /** @} */

private:

    /** @name VAR - what this law remembers
     *  @{ */

    /** @brief The instrument, borrowed: the firmware owns it, registers it
     *  and binds its geo parameters. Null in a build without SUN_TRACKER,
     *  which is a build this program is not in either. */
    Sun::SunPositionTracker* const _sun;

    /** @} */

public:
    SunWatch(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "sun_watch", Tasks::GROUP_SLOW, io), _sun(ctx.sun) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
    void observe(ProgramContext& c) override;  ///< reports in EVERY state
};

inline void SunWatch::bind(Vars::Bank& io) {
    from_rise = _bind_output<int16_t>(io, "sol_fromRise_min");
    from_set  = _bind_output<int16_t>(io, "sol_fromSet_min");
    elevation = _bind_output<float>(io, "sol_elev_deg");
    // The offsets ARE the sun. A tracker that is not here never starts being
    // here, so this law has nothing to publish for the life of the run.
    if (_sun == nullptr) invalidate();
}

}  // namespace Site

#endif // SITE_PROGRAM_SUN_WATCH_H
