#ifndef SITE_PROGRAM_SCREEN_CONTROLLER_H
#define SITE_PROGRAM_SCREEN_CONTROLLER_H

/**
 * @file screen_controller.h
 * @brief HAND-WRITTEN - one coil, deployed when the greenhouse is losing heat.
 */

#include <cstdint>

#include "../../../vars.h"
#include "screen_controller.gen.h"

namespace Site {

using namespace Botanik;

class ScreenController final : public Gen::ScreenController {

    bool       _deployed = false;
    bool       _seeded   = false;
    bool       _command_changed = false;

public:
    ScreenController(const SetupContext& ctx, Vars::Bank& io)
        : Gen::ScreenController(ctx, io) {}

protected:
    void run(ProgramContext& c) override;
    void observe(ProgramContext& c) override;

private:
    /** @brief Command the screen, starting a traverse if that is a change. */
    void _command(bool deploy);

    /** @brief Whether @p phase allows a move in that direction. The spec's
     *  permission table: the day's direction decides what the fabric may do,
     *  and the demand only decides whether it wants to. */
    static bool _allows(DayPhase phase, bool deploy);
};

}  // namespace Site

#endif // SITE_PROGRAM_SCREEN_CONTROLLER_H
