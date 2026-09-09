#ifndef DEMO_SITE_H
#define DEMO_SITE_H

/**
 * @file site.h
 * @brief The demo site: the generated tables, and nothing else.
 *
 * A site is DATA. Everything this one is - its rows, its enums, its home
 * screen, its field devices, its `SiteDescriptor`, the firmware bound to it -
 * is generated from the CSVs in `tables/` and from `site.toml` by
 * `scripts/tables.py`. Its LAWS are the ST under `programs/`, compiled by
 * matiec, and the base owns the resource that runs them. There is no
 * subclass and no hook.
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

// RELATIVE, like the table below, and OUTSIDE the namespace: it opens its own,
// and the ST binding in it is not a site-scoped thing.
#include "generated/enums.gen.h"

namespace Site {

using namespace Botanik;
using namespace Botanik::Vars;

// RELATIVE on purpose, and must stay that way. A site can live outside this
// tree (-DBOTANIK_SITE_DIR) with no include directory of its own, so
// "sites/demo/..." would silently resolve to the IN-TREE reference site
// instead of the one being built.
#include "generated/vars.gen.h"

}  // namespace Site

// The simulated field. Body compiles only under TARGET_SIMULATOR, and is
// included after the variable tables it animates.
#include "sim.h"

#endif // DEMO_SITE_H