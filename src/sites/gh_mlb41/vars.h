#ifndef SITE_VARS_H
#define SITE_VARS_H

/**
 * @file vars.h
 * @brief HAND-WRITTEN - what the device map means, and the home screen.
 *
 * The rows themselves live in the CSV tables under `tables/` and are generated
 * into `generated/vars.gen.h`, included below. This file holds the two things
 * no table expresses: the reading of the site's Modbus device map that the
 * bindings were derived from, and the home-screen captions.
 *
 * Bus: RS485, ModbusRTU master, 38400 baud, none/8/1, on COM2.
 * (BOTANIK_MODBUS_BAUD is pinned to match, in this site's config.h.)
 * 20 field devices, 102 mapped signals, 13 free channels. (The published map
 * lists 70, and omits MTR-2 object 2, active power, which the device answers.)
 *
 *   SRR/TAC/MTR-1  Eastron SDM630    addr 5   37 readings
 *   SRR/TAC/MTR-2  Lettel MCT1-140   addr 6    6 readings
 *   SRR/TDC/AI-1   Waveshare 25767   addr 11   5 readings (+3 free)
 *   SRR/TDC/AO-1   Waveshare 26211   addr 16   2 drives   (+6 free)
 *   SRR/TDC/RO-1   Waveshare 26517   addr 31   6 coils (+2 free)
 *   SRR/TDC/RO-2   Waveshare 26517   addr 32   6 coils (+2 free)
 *   SRR/TDC/RO-3   Waveshare 26517   addr 33   8 coils
 *   SRR/TDC/RO-4   Waveshare 26517   addr 34   8 coils
 *   SRR/CLI/TH-1..3   temp/RH           addr 41-43  exterior
 *   SRR/CLI/TH-4..6   temp/RH           addr 44-46  crawlspace
 *   SRR/CLI/TH-7..9   temp/RH           addr 47-49  low level
 *   SRR/CLI/TH-10..12 temp/RH           addr 50-52  high level
 *
 * Everything is XP_MODBUS today: the site has no XP_LOCAL variables yet.
 *
 * @warning Binding::reg is the Modbus REGISTER ADDRESS the master puts on the
 * wire: the coil number on the relay boards, and the protocol START ADDRESS on
 * the meters (30001 -> 0, 30003 -> 2, ...). NEVER the device profile's object
 * index - nothing translates one into the other, and a row carrying the index
 * decodes half of the row before it. Nor the register column of the device map,
 * which is the Opta's own Modbus *slave* address and belongs in the slave map.
 * Each row's notes record the object index, so a reading can still be traced
 * back to the published map.
 *
 * @note The meters answer FC04 (input registers). The SDM630 manual requires it;
 * the MCT1-140 accepts 03 or 04 and is given 04 to match.
 *
 * @note Both meters publish IEEE-754 singles, high word at the lower address, in
 * direct engineering units, so these rows are 1:1 and carry no calibration. The
 * MCT1-140's "unite et precision" column (0.1V, 0.01A, 10W) is RESOLUTION, not a
 * scale factor - except on the Long energy counters, where it is a multiplier
 * (10 Wh per count).
 */

#include <cstdint>

#include "libbotanik.h"

#include "generated/vars.gen.h"

namespace Site {

using namespace Botanik;
using namespace Botanik::Vars;

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
    { "Dose",      "irr_volume_L" },
    { "Water in",  "irr_inTotal_L" },
    { "Local V",   "sys_local_V" },
    { "Local A",   "sys_local_A" },
    { "Grid A",    "grid_total_A" },
    { "Lockout",   "sys_supply_lockout" },
};
const uint8_t g_home_field_count =
    (uint8_t)(sizeof(g_home_fields) / sizeof(g_home_fields[0]));
/** @brief The same count as a constant expression, to size the resolved-ref
 *  array in the store above. */
static const uint16_t HOME_FIELDS = sizeof(g_home_fields) / sizeof(g_home_fields[0]);

/** @} */

// The home screen silently ignores fields past its columns, so pin the count.
static_assert(sizeof(g_home_fields) / sizeof(g_home_fields[0]) <= Config::HOME_FIELDS_MAX,
              "too many home fields: the HomePage would silently drop the tail");

/** @brief Where the home fields' names resolve to, filled at bring-up
 *  (BotanikFirmware::setup). VAR_NONE until then, which reads as the inert sink
 *  rather than as row 0. DECLARED here, DEFINED in vars.cpp: a namespace-scope
 *  array defined in a header is a definition in every including TU.
 *
 *  The last array this site owns. The value store, the string pool and the name
 *  index sat beside it until the bank started owning its own storage - sized
 *  once by Config::VAR_MAX, rather than by every site counting its own rows. */
extern VarRef home_refs[HOME_FIELDS];

}  // namespace Site

#endif // SITE_VARS_H
