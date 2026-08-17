#ifndef LIBBOTANIK_CONFIG_VARS_H
#define LIBBOTANIK_CONFIG_VARS_H

/** @file vars.h
 *  @brief How large a site's variable table and its stores may be. */

#include "libbotanik/config/base.h"

// Defined and undefined per file, not in base.h: see base.h.
#define BOTANIK_CFG(type, name) static constexpr type name = BOTANIK_##name

namespace Botanik { namespace Config {

    /** @name Site table capacity
     *
     *  Bounds a site's generated tables must respect; past each is a silent
     *  runtime truncation. The authoritative source of each (HomePage geometry,
     *  ScalarField's offset type) static_asserts it still agrees.
     *  @{ */

    /** @brief Home-screen readings the HomePage lays out (its three columns
     *  summed); extras are ignored at render. Pinned in pages/home.h. */
#ifndef BOTANIK_HOME_FIELDS_MAX
#  define BOTANIK_HOME_FIELDS_MAX 22
#endif
    BOTANIK_CFG(uint8_t, HOME_FIELDS_MAX);

    /** @brief Components ConfigBinder can carry bindings for. One entry per
     *  configurable component, library and site together; past it a binding is
     *  dropped and that component keeps its constructed defaults. */
#ifndef BOTANIK_CONFIG_BINDINGS_MAX
#  define BOTANIK_CONFIG_BINDINGS_MAX 16
#endif
    BOTANIK_CFG(uint8_t, CONFIG_BINDINGS_MAX);

    /** @brief Distinct variable groups a site may have - a group being a name's
     *  prefix up to the first '_' (vars/access.h). Past it, var_group() answers
     *  "?" and the MONITOR / PARAMS view modes stop gaining rows.
     *  @note The pool covers the WHOLE table, so the library's own eight config
     *  groups count against it. Cheap to raise. */
#ifndef BOTANIK_VAR_GROUPS_MAX
#  define BOTANIK_VAR_GROUPS_MAX 24
#endif
    BOTANIK_CFG(uint8_t, VAR_GROUPS_MAX);

    /** @brief Bytes one interned group name may occupy, NUL included; a longer
     *  prefix truncates. */
#ifndef BOTANIK_VAR_GROUP_NAME_LEN
#  define BOTANIK_VAR_GROUP_NAME_LEN 16
#endif
    BOTANIK_CFG(size_t, VAR_GROUP_NAME_LEN);

    /** @brief The most rows a Bank can hold, across every registered segment.
     *  Sizes the bank's OWN storage (34 bytes a row); the descriptors stay in
     *  the caller's flash.
     *  @note Too small is not silent: rows past the ceiling are REFUSED at
     *  registration and the POST fails naming the first. */
#ifndef BOTANIK_VAR_MAX
#  define BOTANIK_VAR_MAX 512
#endif
    BOTANIK_CFG(uint16_t, VAR_MAX);

    /** @brief How many separate row segments a Bank accepts, one per
     *  register_vars() call. The ceiling exists only so the segment table is a
     *  fixed array. */
#ifndef BOTANIK_VAR_SEGMENTS
#  define BOTANIK_VAR_SEGMENTS 8
#endif
    BOTANIK_CFG(uint8_t, VAR_SEGMENTS);

    /** @brief Bytes of string pool the bank carries for its T_STR rows. A row
     *  that finds no room gets STR_NO_SLOT and reads empty; the library's own
     *  need Vars::LIBRARY_STR_BYTES. */
#ifndef BOTANIK_STR_POOL_BYTES
#  define BOTANIK_STR_POOL_BYTES 1024
#endif
    BOTANIK_CFG(uint16_t, STR_POOL_BYTES);

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

    /** @brief Distinct AnalogCal blocks the bank caches tunable indices for.
     *  Blocks are SHARED by pointer, so this counts blocks and not rows
     *  (gh_mlb41: 6). A table with more loses the cache on the tail, which
     *  BOTANIK_ASSERT catches - the same shape as ALARM_SLOTS. */
#ifndef BOTANIK_ANALOG_BLOCKS_MAX
#  define BOTANIK_ANALOG_BLOCKS_MAX 16
#endif
    BOTANIK_CFG(uint8_t, ANALOG_BLOCKS_MAX);

    /** @brief Rows that carry a WIRE value beside their engineering one - the
     *  AI/AO rows (gh_mlb41: 7). Every other row's wire value IS its value, so
     *  it costs nothing. 0xFF is the "none" index, so 255 is the ceiling. */
#ifndef BOTANIK_ANALOG_ROWS_MAX
#  define BOTANIK_ANALOG_ROWS_MAX 32
#endif
    BOTANIK_CFG(uint8_t, ANALOG_ROWS_MAX);

    /** @} */

} }

#undef BOTANIK_CFG

#endif // LIBBOTANIK_CONFIG_VARS_H
