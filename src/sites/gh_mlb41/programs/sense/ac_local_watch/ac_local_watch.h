#ifndef SITE_PROGRAM_AC_LOCAL_WATCH_H
#define SITE_PROGRAM_AC_LOCAL_WATCH_H

/**
 * @file ac_local_watch.h
 * @brief HAND-WRITTEN - the generator-backed side of the transfer switch, watched.
 */

#include <cstdint>

#include "ac_local_watch.gen.h"

namespace Site {

using namespace Botanik;

class AcLocalWatch final : public Gen::AcLocalWatch {

public:
    AcLocalWatch(const SetupContext& ctx, Vars::Bank& io)
        : Gen::AcLocalWatch(ctx, io) {}

protected:
    void on_bind() override { supply.attach(*this); }
    void run(ProgramContext& c) override { (void)c; }
    void observe(ProgramContext& c) override;
};

}  // namespace Site

#endif // SITE_PROGRAM_AC_LOCAL_WATCH_H
