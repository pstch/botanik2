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

    Sun::SunPositionTracker _sun;

public:
    SunWatch(const SetupContext& ctx, Vars::Bank& io)
        : Gen::SunWatch(ctx, io), _sun(ctx) {}

    /** @brief The instrument, for the config binder. Nothing else needs it. */
    Sun::SunPositionTracker& tracker() { return _sun; }

    /** @brief This program's own entry, and the tracker's: the recompute is a
     *  slow SERVICE task of its own, not something a control phase does. */
    void register_tasks() override {
        Program::register_tasks();
        _sun.register_tasks();
    }

protected:
    void run(ProgramContext& c) override { (void)c; }
    void observe(ProgramContext& c) override;

private:
    /** @brief Whole minutes, clamped to a day either side of the event. */
    static int16_t _minutes(int64_t v);
};

}  // namespace Site

#endif // SITE_PROGRAM_SUN_WATCH_H
