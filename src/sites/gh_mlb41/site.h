#ifndef SITE_H
#define SITE_H

/**
 * @file site.h
 * @brief The gh_mlb41 site: the generated tables, and the firmware binding.
 *
 * A site is DATA. Everything this one is - its rows, its enums, its home
 * screen, its field devices, its `SiteDescriptor` - is generated from
 * `tables/*.csv` and `site.toml` by `scripts/tables.py`. Its LAWS are
 * `programs/**\/*.st`, compiled by matiec, and the base owns the resource that
 * runs them. What is left here is the subclass, which overrides nothing.
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

namespace Site {

using namespace Botanik;
using namespace Botanik::Vars;

// RELATIVE on purpose, and must stay that way. A site can live outside this
// tree (-DBOTANIK_SITE_DIR) with no include directory of its own, so
// "sites/gh_mlb41/..." would silently resolve to the IN-TREE reference site
// instead of the one being built.
#include "generated/vars.gen.h"

}  // namespace Site

/** @brief The gh_mlb41 greenhouse firmware: the generic base, handed this
 *  site's descriptor. It declares no pages, no components and no programs, and
 *  overrides nothing. A site needing an extra page or a component of its own
 *  would override register_pages() / on_setup(); see
 *  libbotanik/firmware/firmware.h for the extension model. */
class GreenhouseMLB41 final : public Botanik::BotanikFirmware {
public:
    GreenhouseMLB41() : Botanik::BotanikFirmware(Site::g_site) {}
};

// The simulated field. Body compiles only under TARGET_SIMULATOR, and is
// included after the variable tables it animates.
#include "sim.h"

/** @brief The firmware type the harness instantiates for this site. */
using BotanikSite = GreenhouseMLB41;

#endif // SITE_H
