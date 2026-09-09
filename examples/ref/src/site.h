#ifndef REF_SITE_H
#define REF_SITE_H

/**
 * @file site.h
 * @brief The ref site: the generated tables, and nothing else.
 *
 * A site is DATA. Everything this one is - its rows, its home screen, its
 * field devices, its `SiteDescriptor`, the firmware bound to it - is generated
 * from the CSVs in `tables/` and from `site.toml`. Its one LAW is the ST under
 * `programs/`, compiled by matiec. There is no subclass and no hook.
 *
 * Every site exposes the same surface, so a harness never names the site:
 *   - `Site::firmware()`  the firmware, bound to this site's descriptor
 *   - `Site::simulate()`  the simulated field (sim.h)
 */

#include <cstdint>

#include "libbotanik.h"

// RELATIVE, and OUTSIDE the namespace: it opens its own.
#include "generated/enums.gen.h"

namespace Site {

using namespace Botanik;
using namespace Botanik::Vars;

// RELATIVE on purpose: a site can live outside this tree with no include
// directory of its own, so an absolute path would resolve to the in-tree
// reference site instead of the one being built.
#include "generated/vars.gen.h"

}  // namespace Site

// The simulated field. Body compiles only under TARGET_SIMULATOR, and is
// included after the variable tables it animates.
#include "sim.h"

#endif // REF_SITE_H
