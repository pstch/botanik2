#ifndef VARS_LIBRARY_H
#define VARS_LIBRARY_H

/**
 * @file library.h
 * @brief The variable rows libbotanik itself declares, for a site's table.
 *
 * A site's `g_vars[]` is not only the site's. The library has state of its own
 * that belongs in the variable model rather than beside it - alarms it raises,
 * readings it publishes, parameters it reads - and the bank holds ONE contiguous
 * descriptor array, so those rows have to live in the site's table. This header
 * is the single splice point:
 *
 * @code
 * const Variable g_vars[] = {
 *     BOTANIK_LIBRARY_VARS          // the library's rows, first
 *
 *     ...the site's parameters...   // must come first among the site's own
 *     ...everything else the site declares...
 * };
 * @endcode
 *
 * One `#define`, no comma after it: every row inside carries its own trailing
 * comma, so a group with nothing in it expands to nothing at all and the ones
 * around it still read correctly. Three of the four groups are empty today; the
 * plumbing is here so that filling one is an edit to this file and nothing else.
 *
 * @section lib_order Why it goes first, and why the parameters are last
 *
 * Two ordering rules, and both come from the same place - `Bank` requires the
 * `ROLE_PARAM` rows to be **one contiguous run**, because that is what lets the
 * PARAMS page and the shell address them by a dense 0-based index.
 *
 * So the library's parameters have to sit immediately beside the site's. Sites
 * put their parameters at the top of their table (it reads well and it is the
 * convention), which pins both rules: the library block goes **first**, and its
 * parameters go **last within it**, so the two parameter blocks abut.
 *
 * Nothing else in the model cares about position. Alarm conditions are numbered
 * by table order, so the library's alarms head the ALARMS page - which is fine,
 * they are the ones every unit has.
 *
 * @section lib_regs Register space
 *
 * A parameter or status value carries a hand-assigned Modbus TCP slave register
 * (`Binding::reg`), and an external master's map is a contract. The library
 * reserves **900-999** for its own rows so a site never has to guess which
 * numbers are safe; `tests/test_site_names.cpp` fails on any collision.
 *
 * @section lib_missing Leaving it out
 *
 * A site that omits the splice still builds and boots. What it loses is
 * everything the library expected to find: today, the health monitor's six alarm
 * rows, so no platform-health fault can be raised at all. POST reports each
 * missing row by name, the bank counts and logs every unresolved lookup, and
 * `tests/test_site_names.cpp` fails before either of those is reached.
 */

#include "../alarms/platform.h"   // BOTANIK_PLATFORM_ALARM_VARS
#include "declare.h"

/** @name The four groups
 *
 *  One per kind of row the library may declare, each a concatenation of whatever
 *  subsystem headers contribute to it. A subsystem defines its own macro next to
 *  the code that owns the rows (as `alarms/platform.h` does) and gets added to
 *  the group here - so this file stays the one place that says what the library
 *  puts in a site's table.
 *
 *  Every row must end with a comma, its own, so an empty group vanishes cleanly.
 *  @{ */

/** @brief Alarms the library raises. */
#define BOTANIK_LIBRARY_ALARM_VARS   BOTANIK_PLATFORM_ALARM_VARS

/** @brief Status values the library publishes (MQTT + the Modbus TCP slave).
 *  None yet. The scheduler's per-beat metrics are the obvious first tenant -
 *  they are read off `SchedulerContext` by three separate consumers today. */
#define BOTANIK_LIBRARY_STATUS_VARS

/** @brief Computed values the library exposes to the HMI only. None yet. */
#define BOTANIK_LIBRARY_GLOBAL_VARS

/** @brief Parameters the library reads. None yet - the network / NTP / MQTT
 *  groups are the site's today, which is why their defaults differ per unit.
 *
 *  Last in the aggregate below, and that is load-bearing: see @ref lib_order. */
#define BOTANIK_LIBRARY_PARAM_VARS

/** @} */

/** @brief Every row the library declares. Splice this into the front of a
 *  site's `g_vars[]`, without a trailing comma. */
#define BOTANIK_LIBRARY_VARS        \
    BOTANIK_LIBRARY_ALARM_VARS      \
    BOTANIK_LIBRARY_STATUS_VARS     \
    BOTANIK_LIBRARY_GLOBAL_VARS     \
    BOTANIK_LIBRARY_PARAM_VARS

#endif // VARS_LIBRARY_H
