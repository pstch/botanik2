#ifndef SITE_TASKS_H
#define SITE_TASKS_H

/**
 * @file tasks.h
 * @brief HAND-WRITTEN - the gh_mlb41 control programs.
 *
 * Not generated: this is the one place a site expresses *behaviour* rather than
 * schema, so it sits with commands.h and sim.h rather than with the tables.
 *
 * A program is a plain function handed a ScanContext. It reads and drives
 * variables through typed handles, and keeps whatever state it needs in statics
 * or function blocks. Two rules the scaffolding guarantees, and which the
 * programs below rely on:
 *
 *  - every task in a beat sees the same instant (inputs refresh once, before
 *    any of them runs)
 *  - @c dt_ms is the REAL time since this task last ran, so a timer keeps
 *    correct time even when the scan was late
 */

#include <cstdint>

#include "botanik.h"

#include "io.h"

namespace Site {

using namespace Botanik;
using Botanik::Scan::ScanContext;

/**
 * @brief Supply watchdog: hold the windows shut while the incoming mains is
 * unhealthy.
 *
 * A worked example of the intended shape rather than the final control law.
 * Note what it does NOT do: no clock reads, no direct IO, no knowledge of
 * Modbus. It reads two variables, applies a debounce, and drives four coils.
 */
inline void supply_watch(ScanContext& c) {
    static Scan::Ton  bad_for;      // debounce: ignore a momentary dropout
    static Scan::Sr   lockout;

    // A meter that has stopped answering reads as its default (0 V), which is
    // out of band and therefore unhealthy - the fallback does the right thing
    // here without the program testing valid() at all.
    float ln = c.io.ai(AI_PWR_LN_V).value();
    bool  unhealthy = (ln < 200.0f) || (ln > 260.0f);

    // Reset-dominant: recovery wins, so the lockout cannot stick once the
    // supply is good again.
    lockout(bad_for(unhealthy, 5000, c.dt_ms), !unhealthy);

    // Publish the decision as an internal, so the console, the shell and
    // telemetry can all see WHY the windows are held - a coil reading 0 does
    // not distinguish "not commanded" from "locked out".
    c.io.id(ID_SYS_SUPPLY_LOCKOUT).set(lockout.q());

    if (lockout.q()) {
        c.io.dout(DO_CLI_WINDOW1_OPN).set(false);
        c.io.dout(DO_CLI_WINDOW2_OPN).set(false);
        c.io.dout(DO_CLI_WINDOW3_OPN).set(false);
        c.io.dout(DO_CLI_WINDOW4_OPN).set(false);
    }
}

/**
 * @brief Report the incoming supply frequency as a slow, deadbanded reading.
 *
 * Exists mostly to show a second rate: it runs a tenth as often as the supply
 * watchdog, and the scan drives both from the same table.
 */
inline void supply_report(ScanContext& c) {
    static Scan::Deadband hz;
    c.io.ia(IA_SYS_SUPPLY_HZ).set(hz(c.io.ai(AI_PWR_HZ).value(), 0.05f));
}

/** @brief The site's control programs, in execution order. */
const Scan::Task g_tasks[] = {
    { "supply_watch",  100,  &supply_watch  },
    { "supply_report", 1000, &supply_report },
};
const uint8_t g_task_count = (uint8_t)(sizeof(g_tasks) / sizeof(g_tasks[0]));

/** @brief Per-task diagnostics; one entry per row above.
 *
 *  Declared, not defined: this is mutable state, so a definition in the header
 *  would give every including TU its own copy and break the link the moment two
 *  of them exist (the sim has one, the firmware has two). Defined in io.cpp
 *  alongside the other site value stores. */
extern Botanik::Scan::TaskStats g_task_stats[];

}  // namespace Site

#endif // SITE_TASKS_H
