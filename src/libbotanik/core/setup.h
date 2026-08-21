#ifndef LIBBOTANIK_CORE_SETUP_H
#define LIBBOTANIK_CORE_SETUP_H

/** @file setup.h
 *  @brief SetupContext - what a Component is BUILT with.
 *
 *  Two contexts, and the split is the point:
 *
 *  | | Carries | Read |
 *  |---|---|---|
 *  | SetupContext | the task table to register in, and the log to speak through | at construction |
 *  | SystemContext | the subsystems a reporting surface shows or acts on | per beat, later |
 *
 *  SystemContext cannot do this job. It is filled in BotanikFirmware's
 *  constructor BODY, after every member exists, so a component reading it at
 *  construction would find it empty. This one is built before anything else and
 *  passed down, which is why `register_tasks()` needs no argument: a component
 *  already knows the table it belongs to.
 *
 *  It deliberately does NOT carry the variable bank. Two reasons, and the first
 *  is fatal on its own: Vars::Bank is itself a Component, so a context carrying
 *  a Bank could not construct one. The second is that a signature should still
 *  say whether something touches the field - most components do not, and a
 *  control program is handed the bank through its ProgramContext when it does.
 */

#include <cstdint>

// Forward-declared, never included: this is a bag of borrowed pointers and has
// no business acquiring any of the subsystems behind them.
namespace Botanik { namespace Tasks { class Registry; } }
namespace Botanik { namespace Logs  { class Logger;   } }
namespace Botanik { namespace Sun   { class SunPositionTracker; } }

namespace Botanik {

    /** @brief What every Component is constructed with (see file header).
     *
     *  Borrowed, and all of them outlive every component: the firmware owns
     *  them and declares them first for exactly that reason. */
    struct SetupContext {
        /** @brief Where register_tasks() puts its entries. Non-const:
         *  registering mutates the table, and bring-up is the one moment that
         *  is meant to happen. SystemContext keeps a CONST view of the same
         *  table for the surfaces that only report on it. */
        Tasks::Registry* tasks  = nullptr;
        /** @brief May be null - a component with nowhere to log is a quiet
         *  component, not a broken one (and that is what a test usually
         *  wants). */
        Logs::Logger*    logger = nullptr;
        /** @brief The solar-position instrument, or null when the build has
         *  none. A borrowed pointer, declared unconditionally: the firmware
         *  owns the tracker and hands it to whatever reads it, and gating a
         *  pointer would only move the #if to every consumer.
         *  @note Not a Component's own wiring - it is here because a control
         *  program is built with this and has no other way to reach it. */
        Sun::SunPositionTracker* sun = nullptr;

        SetupContext() = default;
        SetupContext(Tasks::Registry& r, Logs::Logger* log = nullptr)
            : tasks(&r), logger(log) {}
        /** @brief The pointer form, for the one caller that has no table to
         *  give (a test) and for Logger, which builds its own. */
        SetupContext(Tasks::Registry* r, Logs::Logger* log)
            : tasks(r), logger(log) {}
    };

}

#endif // LIBBOTANIK_CORE_SETUP_H
