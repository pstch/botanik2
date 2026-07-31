/**
 * @file vars.cpp
 * @brief GENERATED - the gh_mlb41 variable value store.
 *
 * Mutable state, so it is defined once here rather than in the header: a
 * definition in vars.h would give every including TU its own copy and break the
 * link the moment two of them exist (the sim has one, the firmware has two).
 */

#include "vars.h"

#include "tasks.h"   // g_task_count, for the stats array below

namespace Site {

Value g_var_values[V_COUNT] = {};
char  g_str_pool[1] = {};

/** @brief Per-task diagnostics, declared extern in tasks.h for the same
 *  reason. */
Botanik::Scan::TaskStats g_task_stats[g_task_count] = {};

}  // namespace Site
