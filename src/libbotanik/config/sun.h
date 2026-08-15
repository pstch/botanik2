#ifndef LIBBOTANIK_CONFIG_SUN_H
#define LIBBOTANIK_CONFIG_SUN_H

/** @file sun.h
 *  @brief Site location, for the solar-position tracker.
 *
 *  One concern of the library's configuration. Every value defaults from
 *  an #ifndef-guarded BOTANIK_* macro a site may override; see
 *  libbotanik_config.h, which aggregates these and holds the rules. */

#include "libbotanik/config/base.h"

// Defined and undefined per file, not in base.h: see base.h.
#define BOTANIK_CFG(type, name) static constexpr type name = BOTANIK_##name

namespace Botanik { namespace Config {

    /** @name Sun position tracking
     *  @{ */

    /** @brief Recompute cadence (ms) for the solar-elevation tracker.
     *  @note The elevation drifts ~0.25 deg/min, so a minute between updates
     *        keeps it well under a degree stale for a fraction of the per-beat
     *        trig work. */
#ifndef BOTANIK_SUN_RECOMPUTE_MS
#  define BOTANIK_SUN_RECOMPUTE_MS 60000
#endif
    BOTANIK_CFG(uint32_t, SUN_RECOMPUTE_MS);

    /** @} */

} }

/** @name Parameter factory defaults
 *
 *  Not `Config::` constants: the factory values of the `geo_` rows the library
 *  declares in library_vars.h. The live values are variables, so a unit can be
 *  re-sited from the PARAMS page without a build; these are only what a wiped
 *  store comes back as.
 *  @{ */

#ifndef BOTANIK_GEO_LAT_DEG
#  define BOTANIK_GEO_LAT_DEG   0.0
#endif
#ifndef BOTANIK_GEO_LON_DEG
#  define BOTANIK_GEO_LON_DEG   0.0
#endif

/** @} */

#undef BOTANIK_CFG

#endif // LIBBOTANIK_CONFIG_SUN_H
