#ifndef LIBBOTANIK_CONFIG_VARS_H
#define LIBBOTANIK_CONFIG_VARS_H

/** @file vars.h
 *  @brief How large a site's variable table and its stores may be.
 *
 *  One concern of the library's configuration. Every value defaults from
 *  an #ifndef-guarded BOTANIK_* macro a site may override; see
 *  libbotanik_config.h, which aggregates these and holds the rules. */

#include "libbotanik/config/base.h"       // the BOTANIK_FEATURE_* gates

// Defined and undefined per file rather than in base.h: base.h is guarded,
// so a second include of it would not restore a macro this file undefined.
#define BOTANIK_CFG(type, name) static constexpr type name = BOTANIK_##name

namespace Botanik { namespace Config {

    /** @name Site table capacity
     *
     *  Bounds a site's generated tables must respect; each would otherwise be a
     *  silent runtime truncation (home fields past the three columns ignored, a
     *  table count past its uint8_t counter wrapped, a byte offset past
     *  ScalarField's int16_t field truncated). Each limit's authoritative source
     *  (HomePage geometry, ScalarField's offset type) static_asserts it still
     *  agrees, so the two can never drift.
     *  @{ */

    /** @brief Home-screen readings the HomePage lays out (its three columns
     *  summed); extras are ignored at render. Pinned in pages/home.h. */
#ifndef BOTANIK_HOME_FIELDS_MAX
#  define BOTANIK_HOME_FIELDS_MAX 22
#endif
    BOTANIK_CFG(uint8_t, HOME_FIELDS_MAX);

    /** @brief Distinct variable groups a site may have. A group is the prefix of
     *  a variable's name up to the first '_' (vars/access.h), interned into a
     *  small pool: naming a variable is what files it, so this bounds how many
     *  domains a table may span. Past it, var_group() answers "?" and the
     *  MONITOR / PARAMS view modes stop gaining rows. */
#ifndef BOTANIK_VAR_GROUPS_MAX
#  define BOTANIK_VAR_GROUPS_MAX 16
#endif
    BOTANIK_CFG(uint8_t, VAR_GROUPS_MAX);

    /** @brief Bytes one interned group name may occupy, NUL included, so a
     *  prefix longer than this truncates. Sizes the intern pools in both the
     *  variable table and the pages that group over it. */
#ifndef BOTANIK_VAR_GROUP_NAME_LEN
#  define BOTANIK_VAR_GROUP_NAME_LEN 16
#endif
    BOTANIK_CFG(size_t, VAR_GROUP_NAME_LEN);

    /** @} */

} }

#undef BOTANIK_CFG

#endif // LIBBOTANIK_CONFIG_VARS_H
