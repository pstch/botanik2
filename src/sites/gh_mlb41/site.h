#ifndef SITE_H
#define SITE_H

/**
 * @file site.h
 * @brief HAND-WRITTEN - the gh_mlb41 firmware binding, and the uniform handle
 *        every target harness builds against.
 *
 * Not generated: this is the one file you own and the generator never
 * overwrites. It is deliberately tiny, because a site is data. Everything
 * declarative lives in the generated files this pulls in through descriptor.h:
 *
 *   payload.h      GENERATED  the PLC payload + its layout asserts
 *   status.h       GENERATED  status variables + home-screen readings
 *   params.h       GENERATED  config parameters
 *   alarms.h       GENERATED  alarm conditions
 *   descriptor.h   GENERATED  the SiteDescriptor bundling them
 *   commands.h     hand-written  shell commands (code, not schema)
 *   sim.h          hand-written  the mock PLC: animator + emulator instance
 *
 * Every site exposes the same surface, so a harness never names the site:
 *   - `BotanikSite`           the firmware type to instantiate
 *   - `Site::simulate()`      the simulated field (sim.h)
 *   - `dilated_timeout()` sim-only timeout dilation (sim.h)
 *
 * A target selects a site by directory (-DBOTANIK_SITE_DIR, or -DBOTANIK_SITE
 * for one in the library tree); CMake points BOTANIK_SITE_HEADER at this file.
 * All includes here are RELATIVE, so this directory works unchanged whether it
 * lives in the library tree or in a consumer's project.
 */

#include "descriptor.h"   // Site::g_site + every generated table (and payload.h)

// ===========================================================================
// GreenhouseMLB41 - the gh_mlb41 site firmware (derives BotanikFirmware)
// ===========================================================================

/**
 * @brief The gh_mlb41 greenhouse firmware.
 *
 * A thin binding of the generic base to this site's PLC payload + descriptor:
 * it declares no pages and overrides no hooks. The base owns every page
 * (home/shell/logs/alarms/params/monitor) and wires the descriptor's tables
 * (alarms, commands, status variables, config parameters) generically, so the
 * site is data (the generated tables), not code. A site that needs custom
 * per-beat logic or an extra page (e.g. re-adding a STATUS page by subclassing
 * Pages::StatusPage) would override the base's on_step() / register_pages()
 * here. See libbotanik/core/firmware.h for the extension model.
 */
class GreenhouseMLB41 final : public Botanik::BotanikFirmware {
public:
    /** @brief Hand the base this site's descriptor; the base owns the data
     *  struct the tables address. */
    GreenhouseMLB41()
        : Botanik::BotanikFirmware(Site::g_site)
    {}
};

// The mock PLC (animator + Site::MockPlc). Body compiles only under
// TARGET_SIMULATOR; near-empty on OPTA, where the real coprocessor plays that
// role. Included after the payload it animates.
#include "sim.h"

/** @brief The firmware type the harness instantiates for this site. */
using BotanikSite = GreenhouseMLB41;

#endif // SITE_H
