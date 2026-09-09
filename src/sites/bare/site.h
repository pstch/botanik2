#ifndef BARE_SITE_H
#define BARE_SITE_H

/**
 * @file site.h
 * @brief The bare site: the generated table, and nothing else.
 *
 * The minimum a site can be. It declares no variables of its own, no programs,
 * no commands and no simulated field, which is the point: what builds here is
 * libbotanik with every optional feature compiled out, so the seam that makes
 * them optional is exercised by something rather than asserted by a comment.
 * The table under `generated/` is the library's own rows and nothing else.
 *
 * Same surface as any other site, so a harness never names it:
 *   - `Site::firmware()`  the firmware, bound to this site's descriptor
 *   - `Site::simulate()`  the simulated field, which here has nothing to animate
 */

#include <cstdint>

#include "libbotanik.h"

#include "generated/enums.gen.h"

namespace Site {

using namespace Botanik;
using namespace Botanik::Vars;

// RELATIVE on purpose: a site can live outside this tree with no include
// directory of its own, so an absolute path would resolve to the in-tree
// reference site instead of the one being built.
#include "generated/vars.gen.h"

#ifdef TARGET_SIMULATOR
/** @brief No field to animate: this site declares no rows of its own. */
inline void simulate(Botanik::Vars::Bank& io, uint32_t now_ms) {
    (void)io; (void)now_ms;
}
#endif

}  // namespace Site

#endif // BARE_SITE_H
