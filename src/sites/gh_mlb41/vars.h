#ifndef SITE_VARS_H
#define SITE_VARS_H

/**
 * @file vars.h
 * @brief The site's variables: the counts and enums the generated table
 *        carries, the home screen, and the two things a table cannot say.
 *
 * The rows themselves are `tables/*.csv` by way of `scripts/tables.py
 * generate`. What is here is the HOME SCREEN - a caption is a person's choice,
 * and nothing in a row's name says it belongs on the landing page - and the
 * two guards below, which are about the BUILD rather than about any row.
 *
 * What is on the field bus, and what each unit will answer: README.md.
 */

#include <cstdint>

#include "libbotanik.h"

namespace Site {

using namespace Botanik;
using namespace Botanik::Vars;


// This site turns every optional feature ON, and the row count above says
// so as a flat number rather than a sum of #ifs. Build it with one off
// and the table reserves rows the library never registers, which
// misaligns every register after them - so refuse instead of drifting.
#if !(BOTANIK_FEATURE_CONSOLE     \
      && BOTANIK_FEATURE_HOME_PAGE     \
      && BOTANIK_FEATURE_SHELL_PAGE     \
      && BOTANIK_FEATURE_LOGS_PAGE     \
      && BOTANIK_FEATURE_LOGGING     \
      && BOTANIK_FEATURE_LOGS_MONITOR     \
      && BOTANIK_FEATURE_LOGS_PUBLISH     \
      && BOTANIK_FEATURE_NETWORK     \
      && BOTANIK_FEATURE_NTP     \
      && BOTANIK_FEATURE_MQTT     \
      && BOTANIK_FEATURE_PLC     \
      && BOTANIK_FEATURE_MODBUS_RTU_CLIENT     \
      && BOTANIK_FEATURE_MODBUS_TCP_SERVER     \
      && BOTANIK_FEATURE_SUN_TRACKER)
#error "gh_mlb41 assumes every optional feature is enabled"
#endif

/** @brief Rows the library registers ahead of this site's, into the same
 *  table. A flat count, because this site turns every optional feature ON.
 *  HAND-KEPT, and it cannot be taken from the library: `vars_table.h` is
 *  included by the firmware's wiring TU alone, never standalone.
 *  @note It went stale once - 66 against 70 - when the sun tracker's rows
 *  moved into the library table. A stale number lets the guard below pass on a
 *  table that then OVERFLOWS, and the bank drops whatever did not fit in
 *  silence. `Bank::refused_count()` is what actually catches that, and the
 *  suite asserts it is zero. */
const uint16_t LIBRARY_ROWS = 70;



/** @brief Everything the tables generate: the calibration blocks, the two
 *  Descriptor arrays and their counts, the pulse inputs and the field-bus
 *  device map. Edit the CSVs under tables/ and run `scripts/tables.py generate`. */
#include "generated/vars.gen.h"

// register_vars() adds NONE of a segment that does not fit, so one row
// past the limit costs a whole table: an empty field-bus plan and a
// failing POST, in the field. Caught here instead.
static_assert(LIBRARY_ROWS + PARAMS_COUNT + ROWS_COUNT <= Config::VAR_MAX,
              "site table exceeds VAR_MAX - raise BOTANIK_VAR_MAX (about 36 bytes of RAM per row)");

/** @name Home screen - the one table no rule derives
 *
 *  A variable's group and whether it is published follow from its name and
 *  scope. A home field does not: the caption is a person's choice and nothing in
 *  the name says the row belongs on the landing page, so it is declared.
 *  @{ */

/** @brief Home-screen readings: label + variable. Laid out in three columns of
 *  8, column-major; column 1 holds 6 (its top two cells carry the uptime readout
 *  and a spacer). The geometry caps labels at 11 chars and values at 13. */
const HomeVar g_home_fields[] = {
    // column 1 (6): what the plant is doing, before why
    { "Interior",  "cli_temp_in" },
    { "Exterior",  "cli_temp_ext" },
    { "Humidity",  "cli_rh_hi" },
    { "Dew margin","cli_dpMargin_degC" },
    { "Target",    "cli_targetTemp_degC" },
    { "Phase",     "cli_phase" },
    // column 2 (8): the three ladders, and what they came to
    { "Cool dem",  "cli_coolDem" },
    { "Strat dem", "cli_stratDem" },
    { "Humid dem", "cli_humidDem" },
    { "Windows",   "cli_windowDem" },
    { "Intractor", "cli_intr1" },
    { "Fan speed", "cli_fanSpeed" },
    { "Screen",    "cli_screen" },
    { "Skirt",     "cli_skirtDuty" },
    // column 3 (8): irrigation, then the supply that has to be there for it
    { "Irrigation","irr_state" },
    { "Tray",      "irr_activeTray" },
    { "Since rain","cli_sinceRain_min" },
    { "Wind",      "cli_wind_mps" },
    { "Local V",   "sys_local_V" },
    { "Local A",   "sys_local_A" },
    { "Grid A",    "grid_total_A" },
    { "Lockout",   "sys_supply_lockout" },
};
const uint8_t g_home_field_count =
    (uint8_t)(sizeof(g_home_fields) / sizeof(g_home_fields[0]));

/** @} */

// The home screen silently ignores fields past its columns, so pin the count.
static_assert(g_home_field_count <= Config::HOME_FIELDS_MAX,
              "too many home fields: the HomePage would silently drop the tail");

/** @brief Where the home fields' names resolve to, filled at bring-up
 *  (BotanikFirmware::setup). VAR_NONE until then, which reads as the inert sink
 *  rather than as row 0. DECLARED here, DEFINED in vars.cpp: a namespace-scope
 *  array defined in a header is a definition in every including TU.
 *
 *  The last array this site owns. The value store, the string pool and the name
 *  index sat beside it until the bank started owning its own storage - sized
 *  once by Config::VAR_MAX, rather than by every site counting its own rows. */
extern VarRef home_refs[g_home_field_count];

}  // namespace Site

#endif // SITE_VARS_H
