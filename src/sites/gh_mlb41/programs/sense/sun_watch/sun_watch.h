#ifndef SITE_PROGRAM_SUN_WATCH_H
#define SITE_PROGRAM_SUN_WATCH_H

/**
 * @file sun_watch.h
 * @brief HAND-WRITTEN - where the sun is, published as rows.
 */

#include <cstdint>

#include "sun_watch.gen.h"

namespace Site {

using namespace Botanik;

class SunWatch final : public Gen::SunWatch {

    /** @brief The instrument, borrowed: the firmware owns it, registers it and
     *  binds its geo parameters. Null in a build without SUN_TRACKER, which is
     *  a build this program is not in either. */
    Sun::SunPositionTracker* const _sun;

public:
    SunWatch(const SetupContext& ctx, Vars::Bank& io)
        : Gen::SunWatch(ctx, io), _sun(ctx.sun) {}

protected:
    void run(ProgramContext& c) override { (void)c; }
    void observe(ProgramContext& c) override;

private:
    /** @brief Whole minutes, clamped to a day either side of the event. */
    static int16_t _minutes(int64_t v);
};

}  // namespace Site

#endif // SITE_PROGRAM_SUN_WATCH_H
