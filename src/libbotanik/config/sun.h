#ifndef LIBBOTANIK_CONFIG_SUN_H
#define LIBBOTANIK_CONFIG_SUN_H

/** @file sun.h
 *  @brief Site location, for the solar-position tracker.
 *
 *  One concern of the library's configuration. Every value defaults from
 *  an #ifndef-guarded BOTANIK_* macro a site may override; see
 *  libbotanik_config.h, which aggregates these and holds the rules. */

#include "libbotanik/config/base.h"       // the BOTANIK_FEATURE_* gates

// Defined and undefined per file rather than in base.h: base.h is guarded,
// so a second include of it would not restore a macro this file undefined.
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

#undef BOTANIK_CFG

#endif // LIBBOTANIK_CONFIG_SUN_H
