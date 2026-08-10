#ifndef LIBBOTANIK_CONFIG_VARS_H
#define LIBBOTANIK_CONFIG_VARS_H

/** @file vars.h
 *  @brief How large a site's variable table and its stores may be.
 *
 *  One concern of the library's configuration. Every value defaults from
 *  an #ifndef-guarded BOTANIK_* macro a site may override; see
 *  libbotanik_config.h, which aggregates these and holds the rules. */

#include "libbotanik/config/base.h"       // the BOTANIK_FEATURE_* gates

// Defined and undefined per file, not in base.h: see base.h.
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
     *  MONITOR / PARAMS view modes stop gaining rows.
     *
     *  The intern pool covers the WHOLE table, parameters and alarms included -
     *  not just the published rows - so the library's own config groups count
     *  against it. There are eight of those today (serial, net, ntp, mqtt, geo,
     *  mb, term, sys), which is most of the reason this is not 8: `gh_mlb41`
     *  reaches 15 with seven groups of its own. Cheap to raise - one pool of
     *  VAR_GROUP_NAME_LEN bytes per slot, plus a pointer in two pages. */
#ifndef BOTANIK_VAR_GROUPS_MAX
#  define BOTANIK_VAR_GROUPS_MAX 24
#endif
    BOTANIK_CFG(uint8_t, VAR_GROUPS_MAX);

    /** @brief Bytes one interned group name may occupy, NUL included, so a
     *  prefix longer than this truncates. Sizes the intern pools in both the
     *  variable table and the pages that group over it. */
#ifndef BOTANIK_VAR_GROUP_NAME_LEN
#  define BOTANIK_VAR_GROUP_NAME_LEN 16
#endif
    BOTANIK_CFG(size_t, VAR_GROUP_NAME_LEN);

    /** @brief How often the F_RETAIN rows are written to storage, in ms. 0
     *  turns the cadence off, leaving only the save on the way down.
     *
     *  A wear-versus-loss dial: writing a totalizer through would be a flash
     *  erase every beat, while five minutes costs ~300 erases a day per row and
     *  bounds an unexpected power cut to five minutes of lost counting. A
     *  DELIBERATE stop loses nothing either way (the shutdown path writes them
     *  out). Five minutes because these rows - litres, hours run - are read in
     *  days and weeks; a site counting by the minute should say so here. */
#ifndef BOTANIK_RETAIN_SAVE_MS
#  define BOTANIK_RETAIN_SAVE_MS 300000
#endif
    BOTANIK_CFG(uint32_t, RETAIN_SAVE_MS);

    /** @} */

} }

#undef BOTANIK_CFG

#endif // LIBBOTANIK_CONFIG_VARS_H
