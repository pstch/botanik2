#ifndef LIBBOTANIK_CONFIG_SIMULATOR_H
#define LIBBOTANIK_CONFIG_SIMULATOR_H

/** @file simulator.h
 *  @brief Host-build scaffolding; nothing here reaches the OPTA.
 *
 *  One concern of the library's configuration. Every value defaults from
 *  an #ifndef-guarded BOTANIK_* macro a site may override; see
 *  libbotanik_config.h, which aggregates these and holds the rules. */

#include "libbotanik/config/base.h"       // the BOTANIK_FEATURE_* gates

// Defined and undefined per file, not in base.h: see base.h.
#define BOTANIK_CFG(type, name) static constexpr type name = BOTANIK_##name

namespace Botanik { namespace Config {

    /** @name Simulator only
     *  @{ */

#ifdef TARGET_SIMULATOR

    /** @brief Cap on the simulator's whole key-value store file (bytes).
     *  @note The OPTA's KVStore is a TDBStore on the external QSPI flash, bounded
     *        by its region and not by this, so this is sim-only. The sim file
     *        holds every key at once and hex-encodes values, so it needs twice
     *        the largest value plus room for every parameter. */
#ifndef BOTANIK_STORE_MAX_BYTES
#  define BOTANIK_STORE_MAX_BYTES 16384
#endif
    BOTANIK_CFG(uint32_t, STORE_MAX_BYTES);

    // The check that this holds the notes blob lives in pages/notes.h: the
    // constraint is between this cap and NOTES_LEN, and the notes page is what
    // writes the largest single value the store ever sees.

#endif // TARGET_SIMULATOR

    /** @} */

} }

#undef BOTANIK_CFG

#endif // LIBBOTANIK_CONFIG_SIMULATOR_H
