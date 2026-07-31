/**
 * @file io.cpp
 * @brief GENERATED - storage for the gh_mlb41 IO value stores.
 *
 * The tables in io.h are const and live in flash; their values do not, so they
 * get exactly one definition here rather than one copy per including TU.
 */

#include "io.h"
#include "tasks.h"

namespace Site {

AnalogValue  g_analog_in_values[AI_COUNT]   = {};
DigitalValue g_digital_out_values[DO_COUNT] = {};
AnalogValue  g_internal_analog_values[IA_COUNT] = {};
DigitalValue g_internal_digital_values[ID_COUNT] = {};

/** @brief Per-task diagnostics for the programs in tasks.h. */
Botanik::Scan::TaskStats g_task_stats[g_task_count] = {};

}  // namespace Site
