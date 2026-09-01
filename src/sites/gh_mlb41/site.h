#ifndef GH_MLB41_SITE_H
#define GH_MLB41_SITE_H

/**
 * @file site.h
 * @brief The gh_mlb41 site: the generated tables, and nothing else.
 *
 * A site is DATA. Everything this one is - its rows, its enums, its home
 * screen, its field devices, its `SiteDescriptor`, the firmware bound to it -
 * is generated from `tables/*.csv` and `site.toml` by `scripts/tables.py`. Its
 * LAWS are `programs/**\/*.st`, compiled by matiec, and the base owns the
 * resource that runs them. There is no subclass and no hook.
 *
 * Every site exposes the same surface, so a harness never names the site:
 *   - `Site::firmware()`  the firmware, bound to this site's descriptor
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

// The simulated field. Body compiles only under TARGET_SIMULATOR, and is
// included after the variable tables it animates.
#include "sim.h"

#endif // GH_MLB41_SITE_H