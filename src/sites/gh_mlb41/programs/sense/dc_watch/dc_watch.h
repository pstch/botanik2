#ifndef SITE_PROGRAM_DC_WATCH_H
#define SITE_PROGRAM_DC_WATCH_H

/**
 * @file dc_watch.h
 * @brief HAND-WRITTEN - the DC rails and the two batteries, watched.
 */

#include <cstdint>

#include "dc_watch.gen.h"

namespace Site {

using namespace Botanik;

class DcWatch final : public Gen::DcWatch {

    /** @brief Per rail, whether its divider has EVER answered. @see AcSupply */
    bool          _seen[4] = { false, false, false, false };
    /** @brief What this law last told the registry, so the plant-wide hold is
     *  pushed on the EDGE and not walked over every task every beat. */
    bool          _safe_held = false;

public:
    DcWatch(const SetupContext& ctx, Vars::Bank& io)
        : Gen::DcWatch(ctx, io) {}

protected:
    void run(ProgramContext& c) override { (void)c; }
    void observe(ProgramContext& c) override;

private:
    /** @brief One rail: average it, mirror it through its deadband, and judge
     *  the average against @p lo.
     *  @param seen this rail's "has ever answered" latch, updated here
     *  @return false when the rail is under its floor, or has gone silent
     *          after answering at least once */
    struct Rail {
        Plc::Ewma&           avg;
        Plc::Deadband&       band;
        Vars::RefIn<float>   in;
        Vars::RefOut<float>  out;
        Vars::RefIn<float>   lo;
        Vars::RefIn<uint16_t> avg_ms;
    };

    static bool _rail(const Rail& r, uint32_t dt_ms, bool& seen);
};

}  // namespace Site

#endif // SITE_PROGRAM_DC_WATCH_H
