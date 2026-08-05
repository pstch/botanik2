#ifndef SITE_H
#define SITE_H

/**
 * @file site.h
 * @brief HAND-WRITTEN - the gh_mlb41 site: its shell commands, the descriptor
 *        bundling every table, the firmware binding, and the uniform handle
 *        every target harness builds against.
 *
 * Not generated: this is the file you own and the generator never overwrites.
 * It stays small because a site is data, and the data lives in the generated
 * tables this pulls in:
 *
 *   vars.h    GENERATED  everything declarative - every variable in one table
 *                        (parameters, alarms, sensor inputs, computed and status
 *                        values, actuator outputs), the analog calibration
 *                        blocks, the home-screen readings, and the RAM value
 *                        store behind them
 *   tasks.h   hand-written  the control programs
 *   sim.h     hand-written  the simulated field
 *
 * What used to be separate files and is not any more: `descriptor.h` (one struct
 * literal, below), `commands.h` (one table, below), `alarms.h` (an alarm is a
 * SCOPE_A row in vars.h), `status.h` (a group is a name prefix; the home fields
 * moved into vars.h), `vars.cpp` (the value store is header-only) and
 * `botanik.h` (the library is reached as `libbotanik.h`).
 *
 * `config.h` stays its own file. It CAN be folded in here - libbotanik_config.h
 * would include this header in a first pass, guarded by a macro, and read only
 * the #defines - but that makes one file answer two questions and be enterable
 * twice, which is a lot of cleverness to save a file that says five things.
 *
 * Every site exposes the same surface, so a harness never names the site:
 *   - `BotanikSite`       the firmware type to instantiate
 *   - `Site::simulate()`  the simulated field (sim.h)
 *   - `dilated_timeout()` sim-only timeout dilation (sim.h)
 *
 * A target selects a site by directory (-DBOTANIK_SITE_DIR, or -DBOTANIK_SITE
 * for one in the library tree); CMake points BOTANIK_SITE_HEADER at this file.
 */

#include <cstdint>
#include <cstdio>    // snprintf, for the command handlers below

#include "libbotanik.h"

#include "vars.h"         // every variable, in one table
#include "tasks.h"        // control programs

namespace Site {

/** @brief The site's shell commands: name, one-line help, handler.
 *
 *  A command is code, not schema. They are matched BEFORE the library built-ins,
 *  so a name reused here shadows one - avoid `exit` / `reset`. The ctx handed to
 *  each handler is the live `const Vars::Bank*`, wired by the firmware base, so
 *  a command reads fresh variable data without holding state of its own. */
const Botanik::Shell::ShellCommand* const g_commands = nullptr;
const uint8_t g_command_count = 0;

/** @brief The whole handoff to the library: name + the site's tables. Field
 *  order matches Botanik::SiteDescriptor; any table may be null / count 0.
 *
 *  Alarms are not here - each is a SCOPE_A row in vars.h carrying its own code
 *  and dwell policy, and the AlarmController finds them by scanning. Neither are the
 *  status groups - a variable's group is the prefix of its name. */
const Botanik::SiteDescriptor g_site = {
    Botanik::Config::SITE_NAME,
    g_commands,      g_command_count,                    // site shell commands
    g_home_fields,   g_home_field_count,  Store::home_refs,  // home-screen readings
    SITE_VAR_TABLES,                                     // variables + values + string pool
    g_tasks,         g_task_count,        Tasks::stats,      // control programs
};

}  // namespace Site

// ===========================================================================
// GreenhouseMLB41 - the gh_mlb41 site firmware (derives BotanikFirmware)
// ===========================================================================

/**
 * @brief The gh_mlb41 greenhouse firmware.
 *
 * A thin binding of the generic base to this site's descriptor: it declares no
 * pages and overrides no hooks. The base owns every page
 * (home/shell/logs/alarms/params/monitor) and wires the descriptor generically,
 * so the site is data, not code. A site that needs custom per-beat logic or an
 * extra page (e.g. re-adding a STATUS page by subclassing Pages::StatusPage)
 * would override the base's on_step() / register_pages() here. See
 * libbotanik/core/firmware.h for the extension model.
 */
class GreenhouseMLB41 final : public Botanik::BotanikFirmware {
public:
    /** @brief Hand the base this site's descriptor; the base owns the data
     *  struct the tables address. */
    GreenhouseMLB41()
        : Botanik::BotanikFirmware(Site::g_site)
    {}
};

// The simulated field (Site::simulate()). Body compiles only under
// TARGET_SIMULATOR; near-empty on OPTA, where the real field plays that role.
// Included after the variable tables it animates.
#include "sim.h"

/** @brief The firmware type the harness instantiates for this site. */
using BotanikSite = GreenhouseMLB41;

#endif // SITE_H
