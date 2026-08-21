#ifndef SITE_PROGRAM_AC_GRID_WATCH_H
#define SITE_PROGRAM_AC_GRID_WATCH_H

/**
 * @file ac_grid_watch.h
 * @brief HAND-WRITTEN - the utility feed, watched.
 */

#include <cstdint>

#include "ac_grid_watch.gen.h"

namespace Site {

using namespace Botanik;

class AcGridWatch final : public Gen::AcGridWatch {

public:
    AcGridWatch(const SetupContext& ctx, Vars::Bank& io)
        : Gen::AcGridWatch(ctx, io) {}

protected:
    void on_bind() override { supply.attach(*this); }
    void run(ProgramContext& c) override { (void)c; }
    void observe(ProgramContext& c) override;
};

}  // namespace Site

#endif // SITE_PROGRAM_AC_GRID_WATCH_H
