#ifndef SITE_TASKS_H
#define SITE_TASKS_H

/**
 * @file tasks.h
 * @brief HAND-WRITTEN - the gh_mlb41 control programs.
 *
 * Not generated: this is the one place a site expresses *behaviour* rather than
 * schema, so it sits with site.h and sim.h rather than with the tables.
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

#include "libbotanik.h"

#include "vars.h"

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
    static Plc::Ton  bad_for;      // debounce: ignore a momentary dropout
    static Plc::Sr   lockout;

    // Names resolved ONCE, into the same function-local statics the blocks
    // above live in. Bank::var(const char*) is a linear strcmp scan of the whole
    // table and every row shares a prefix, so each comparison matches several
    // bytes before diverging: resolving per activation was ~99% of this
    // program's cost (@ref dev_tasks). A VarRef is a bare index - the lookup is
    // the expensive half, and it is the half that cannot change.
    static const Vars::VarRef r_ln      = c.io.find("grid_LN_V");
    static const Vars::VarRef r_lockout = c.io.find("sys_supply_lockout");
    static const Vars::VarRef r_win[]   = { c.io.find("cli_window1_opn"),
                                            c.io.find("cli_window2_opn"),
                                            c.io.find("cli_window3_opn"),
                                            c.io.find("cli_window4_opn") };

    // A meter that has stopped answering reads as its default (0 V), which is
    // out of band and therefore unhealthy - the fallback does the right thing
    // here without the program testing valid() at all.
    float ln = c.io.var(r_ln).value();
    bool  unhealthy = (ln < 200.0f) || (ln > 260.0f);

    // Reset-dominant: recovery wins, so the lockout cannot stick once the
    // supply is good again.
    lockout(bad_for(unhealthy, 5000, c.dt_ms), !unhealthy);

    // Publish the decision as an internal, so the console, the shell and
    // telemetry can all see WHY the windows are held - a coil reading 0 does
    // not distinguish "not commanded" from "locked out".
    c.io.var(r_lockout).set(lockout.q());

    if (lockout.q()) {
        for (const Vars::VarRef& w : r_win) c.io.var(w).set(false);
    }
}

/**
 * @brief Report the incoming supply frequency as a slow, deadbanded reading.
 *
 * Exists mostly to show a second rate: it runs a tenth as often as the supply
 * watchdog, and the scan drives both from the same table.
 */
inline void supply_report(ScanContext& c) {
    static Plc::Deadband hz;
    static const Vars::VarRef r_hz  = c.io.find("sys_supply_hz");
    static const Vars::VarRef r_raw = c.io.find("grid_Hz");
    c.io.var(r_hz).set(hz(c.io.var(r_raw).value(), 0.05f));
}

/** @brief The site's control programs, in execution order. */
const Scan::Task g_tasks[] = {
    { "supply_watch",  100,  &supply_watch  },
    { "supply_report", 1000, &supply_report },
};
const uint8_t g_task_count = (uint8_t)(sizeof(g_tasks) / sizeof(g_tasks[0]));

/** @brief Per-task diagnostics; one entry per row above.
 *
 *  Mutable state in a header, so it uses the same vague-linkage trick as the
 *  value store in vars.h: a class template's static member is emitted per TU and
 *  folded by the linker, where a plain definition here would be a duplicate
 *  symbol the moment two TUs included this. It cannot simply live in vars.h's
 *  store - tasks.h includes vars.h, so it is vars.h that must not know about
 *  tasks. */
template <typename Tag>
struct TaskStore { static Botanik::Scan::TaskStats stats[g_task_count]; };
template <typename Tag>
Botanik::Scan::TaskStats TaskStore<Tag>::stats[g_task_count] = {};

/** @brief The one instantiation, matching Store in vars.h. */
using Tasks = TaskStore<void>;

}  // namespace Site

#endif // SITE_TASKS_H
