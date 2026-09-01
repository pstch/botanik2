#ifndef SITE_H
#define SITE_H

/**
 * @file site.h
 * @brief The gh_mlb41 site: its shell commands, the descriptor bundling every
 *        table, and the firmware binding.
 *
 * A site is DATA, and this is the file that says so: a descriptor pointing at
 * generated tables, and a subclass that overrides nothing. The laws are
 * `programs/**\/*.st`, the rows are `tables/*.csv`, and the base owns the ST
 * resource that runs them.
 *
 * `config.h` stays its own file: CMake includes it before the library defaults.
 *
 * Every site exposes the same surface, so a harness never names the site:
 *   - `BotanikSite`       the firmware type to instantiate
 *   - `Site::simulate()`  the simulated field (sim.h)
 *
 * A target selects a site by directory (-DBOTANIK_SITE_DIR, or -DBOTANIK_SITE
 * for one in the library tree); CMake points BOTANIK_SITE_HEADER at this file.
 */

#include <cstdint>

#include "libbotanik.h"

// Site-internal includes are RELATIVE on purpose, and must stay that way. A
// site can live outside this tree (-DBOTANIK_SITE_DIR), with no include
// directory of its own, so "sites/gh_mlb41/vars.h" would silently resolve to the
// IN-TREE reference site instead of the one being built.
#include "vars.h"

namespace Site {

/** @brief The site's shell commands (Botanik::Shell::ShellCommand): none here.
 *  A site table is matched BEFORE the library built-ins, so a name reused there
 *  shadows one - avoid `exit` / `reset`. */
const Botanik::Shell::ShellCommand* const g_commands = nullptr;
const uint8_t g_command_count = 0;

/** @brief The whole handoff to the library: name + the site's tables. Field
 *  order matches Botanik::SiteDescriptor; any table may be null / count 0.
 *
 *  Alarms are not here (a SCOPE_A row in the variable table) and neither are the
 *  status groups (a variable's group is its name prefix). The device table is
 *  generated, from tables/devices.csv. */
const Botanik::SiteDescriptor g_site = {
    Botanik::Config::SITE_NAME,
    g_commands,     g_command_count,                     // site shell commands
    g_home_fields,  g_home_field_count, home_refs,       // home-screen readings
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
    &g_device_table,                                     // what each unit answers
#endif
    pulse_inputs,   pulse_input_count,                   // edge-counted terminals
    params,         PARAMS_COUNT,                        // ...and the table
    rows,           ROWS_COUNT,
};

}  // namespace Site

// GreenhouseMLB41 - the gh_mlb41 site firmware (derives BotanikFirmware)

/**
 * @brief The gh_mlb41 greenhouse firmware.
 *
 * A thin binding of the generic base to this site's descriptor, and NOTHING
 * else: no pages, no components, no programs, no overrides. Everything this
 * site is, it is by pointing the descriptor at a generated table.
 * A site needing an extra page or a component of its own overrides
 * register_pages() / on_setup() here; see libbotanik/firmware/firmware.h.
 */
class GreenhouseMLB41 final : public Botanik::BotanikFirmware {

public:
    /** @brief Hand the base this site's descriptor. */
    GreenhouseMLB41()
        : Botanik::BotanikFirmware(Site::g_site)
    {}
};

// The simulated field. Body compiles only under TARGET_SIMULATOR, and is
// included after the variable tables it animates.
#include "sim.h"

/** @brief The firmware type the harness instantiates for this site. */
using BotanikSite = GreenhouseMLB41;

#endif // SITE_H
