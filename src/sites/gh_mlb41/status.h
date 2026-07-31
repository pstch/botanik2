#ifndef SITE_STATUS_H
#define SITE_STATUS_H

/**
 * @file status.h
 * @brief GENERATED - the gh_mlb41 status variables: what the console, the
 *        shell and MQTT telemetry publish, and the home-screen readings.
 *
 * Generated from the site's IO map. Regenerating overwrites this file.
 *
 * One table, three consumers, no per-variable code: the MONITOR page, the
 * shell `status` command and MQTT telemetry all resolve these generically
 * against the live variable tables. Each group is published under
 * "<prefix>/telemetry/<group>/<name>", where the leaf is the variable's own
 * name from io.h - there is no second place to keep names in sync.
 *
 * A row is a VarRef: which table, which index. Nothing here carries a byte
 * offset, so re-ordering a variable table cannot silently re-point a status
 * row at its neighbour; a stale index resolves inertly instead.
 */

#include <cstdint>

#include "botanik.h"

#include "io.h"

namespace Site {

using namespace Botanik;
using namespace Botanik::Vars;

/** @name Status groups
 *  @{ */

/** @brief Three-phase power meter (SDM630). */
const VarRef g_grp_power[] = {
    { VAR_ANALOG_IN, AI_PWR_L1_V },
    { VAR_ANALOG_IN, AI_PWR_L2_V },
    { VAR_ANALOG_IN, AI_PWR_L3_V },
    { VAR_ANALOG_IN, AI_PWR_L1_A },
    { VAR_ANALOG_IN, AI_PWR_L2_A },
    { VAR_ANALOG_IN, AI_PWR_L3_A },
    { VAR_ANALOG_IN, AI_PWR_L1_VA },
    { VAR_ANALOG_IN, AI_PWR_L2_VA },
    { VAR_ANALOG_IN, AI_PWR_L3_VA },
    { VAR_ANALOG_IN, AI_PWR_L1_VAR },
    { VAR_ANALOG_IN, AI_PWR_L2_VAR },
    { VAR_ANALOG_IN, AI_PWR_L3_VAR },
    { VAR_ANALOG_IN, AI_PWR_P1_PF },
    { VAR_ANALOG_IN, AI_PWR_P2_PF },
    { VAR_ANALOG_IN, AI_PWR_P3_PF },
    { VAR_ANALOG_IN, AI_PWR_P1_PA },
    { VAR_ANALOG_IN, AI_PWR_P2_PA },
    { VAR_ANALOG_IN, AI_PWR_P3_PA },
    { VAR_ANALOG_IN, AI_PWR_LN_V },
    { VAR_ANALOG_IN, AI_PWR_LL_V },
    { VAR_ANALOG_IN, AI_PWR_AVG_A },
    { VAR_ANALOG_IN, AI_PWR_TOTAL_A },
    { VAR_ANALOG_IN, AI_PWR_TOTAL_W },
    { VAR_ANALOG_IN, AI_PWR_TOTAL_VA },
    { VAR_ANALOG_IN, AI_PWR_TOTAL_VAR },
    { VAR_ANALOG_IN, AI_PWR_TOTAL_PF },
    { VAR_ANALOG_IN, AI_PWR_TOTAL_PA },
    { VAR_ANALOG_IN, AI_PWR_HZ },
    { VAR_ANALOG_IN, AI_PWR_12_V },
    { VAR_ANALOG_IN, AI_PWR_L23_V },
    { VAR_ANALOG_IN, AI_PWR_L31_V },
    { VAR_ANALOG_IN, AI_PWR_LL_AVG_THD },
    { VAR_ANALOG_IN, AI_PWR_LN_AVG_THD },
    { VAR_ANALOG_IN, AI_PWR_N_A },
    { VAR_ANALOG_IN, AI_PWR_L1_W },
    { VAR_ANALOG_IN, AI_PWR_L2_W },
    { VAR_ANALOG_IN, AI_PWR_L3_W },
};

/** @brief Transfer-switch meter (MCT1-140). */
const VarRef g_grp_ats[] = {
    { VAR_ANALOG_IN, AI_ATS_V },
    { VAR_ANALOG_IN, AI_ATS_A },
    { VAR_ANALOG_IN, AI_ATS_W },
    { VAR_ANALOG_IN, AI_ATS_VAR },
    { VAR_ANALOG_IN, AI_ATS_VA },
    { VAR_ANALOG_IN, AI_ATS_HZ },
};

/** @brief Climate actuators: window drives, destratification fans, screen. */
const VarRef g_grp_climate[] = {
    { VAR_DIGITAL_OUT, DO_CLI_WINDOW1_OPN },
    { VAR_DIGITAL_OUT, DO_CLI_WINDOW1_CLO },
    { VAR_DIGITAL_OUT, DO_CLI_WINDOW2_OPN },
    { VAR_DIGITAL_OUT, DO_CLI_WINDOW2_CLO },
    { VAR_DIGITAL_OUT, DO_CLI_WINDOW3_OPN },
    { VAR_DIGITAL_OUT, DO_CLI_WINDOW3_CLO },
    { VAR_DIGITAL_OUT, DO_CLI_DESTRAT1_SPEED1 },
    { VAR_DIGITAL_OUT, DO_CLI_DESTRAT1_SPEED2 },
    { VAR_DIGITAL_OUT, DO_CLI_DESTRAT1_SPEED3 },
    { VAR_DIGITAL_OUT, DO_CLI_SCREEN },
    { VAR_DIGITAL_OUT, DO_CLI_WINDOW4_OPN },
    { VAR_DIGITAL_OUT, DO_CLI_WINDOW4_CLO },
    { VAR_DIGITAL_OUT, DO_CLI_DESTRAT2_SPEED1 },
    { VAR_DIGITAL_OUT, DO_CLI_DESTRAT2_SPEED2 },
    { VAR_DIGITAL_OUT, DO_CLI_DESTRAT2_SPEED3 },
    { VAR_DIGITAL_OUT, DO_CLI_SKIRTVALVE },
};

/** @brief Irrigation valves and tray selectors. */
const VarRef g_grp_irrigation[] = {
    { VAR_DIGITAL_OUT, DO_IRR_INPUT_RO1 },
    { VAR_DIGITAL_OUT, DO_IRR_OUTPUT_RO1 },
    { VAR_DIGITAL_OUT, DO_IRR_TRAY1 },
    { VAR_DIGITAL_OUT, DO_IRR_TRAY2 },
    { VAR_DIGITAL_OUT, DO_IRR_INPUT_RO2 },
    { VAR_DIGITAL_OUT, DO_IRR_OUTPUT_RO2 },
    { VAR_DIGITAL_OUT, DO_IRR_TRAY3 },
    { VAR_DIGITAL_OUT, DO_IRR_TRAY4 },
    { VAR_DIGITAL_OUT, DO_IRR_TRAY5 },
    { VAR_DIGITAL_OUT, DO_IRR_TRAY6 },
    { VAR_DIGITAL_OUT, DO_IRR_TRAY7 },
    { VAR_DIGITAL_OUT, DO_IRR_TRAY8 },
};

/** @brief Alarm conditions. Computed digitals, so the alarms page, the monitor
 *  table and telemetry all read the one set of booleans. */
const VarRef g_grp_alarms[] = {
    { VAR_INTERNAL_DIGITAL, ID_AC_GRID },
    { VAR_INTERNAL_DIGITAL, ID_DC_PS1 },
    { VAR_INTERNAL_DIGITAL, ID_GEN_FLT },
    { VAR_INTERNAL_DIGITAL, ID_AC_LOCAL },
    { VAR_INTERNAL_DIGITAL, ID_DC_PS2 },
    { VAR_INTERNAL_DIGITAL, ID_DC_UPS },
    { VAR_INTERNAL_DIGITAL, ID_DC_LOCAL },
    { VAR_INTERNAL_DIGITAL, ID_GEN_BATT },
    { VAR_INTERNAL_DIGITAL, ID_PLC_FLT },
    { VAR_INTERNAL_DIGITAL, ID_NET_FLT },
    { VAR_INTERNAL_DIGITAL, ID_NTP_TIM },
    { VAR_INTERNAL_DIGITAL, ID_ACT_FLT },
    { VAR_INTERNAL_DIGITAL, ID_SNS_FLT },
    { VAR_INTERNAL_DIGITAL, ID_TOO_HOT },
    { VAR_INTERNAL_DIGITAL, ID_TOO_COLD },
    { VAR_INTERNAL_DIGITAL, ID_TOO_HUM },
    { VAR_INTERNAL_DIGITAL, ID_TMP_STR },
    { VAR_INTERNAL_DIGITAL, ID_WTR_LK },
    { VAR_INTERNAL_DIGITAL, ID_WTR_BAR },
    { VAR_INTERNAL_DIGITAL, ID_WTR_CLOG },
    { VAR_INTERNAL_DIGITAL, ID_IO_DROP },
    { VAR_INTERNAL_DIGITAL, ID_LOG_FLD },
    { VAR_INTERNAL_DIGITAL, ID_WH_STORM },
};

/** @brief Firmware-derived state. */
const VarRef g_grp_system[] = {
    { VAR_INTERNAL_ANALOG,  IA_SYS_SUPPLY_HZ },
    { VAR_INTERNAL_DIGITAL, ID_SYS_SUPPLY_LOCKOUT },
};

#define GRP(nm) { #nm, g_grp_##nm, (uint16_t)(sizeof(g_grp_##nm) / sizeof(g_grp_##nm[0])) }

const VarGroup g_status_groups[] = {
    GRP(power),
    GRP(ats),
    GRP(climate),
    GRP(irrigation),
    GRP(alarms),
    GRP(system),
};
const uint8_t g_status_group_count =
    (uint8_t)(sizeof(g_status_groups) / sizeof(g_status_groups[0]));

#undef GRP

/** @} */

/** @name Home screen
 *  @{ */

/** @brief Home-screen readings: label + variable. The HomePage lays these out
 *  in three columns of 8, filled column-major; column 1 holds 6 (its top two
 *  cells carry the uptime readout and a spacer). Labels are capped at 11 chars
 *  and values at 13 by the column geometry. */
const HomeVar g_home_fields[] = {
    // column 1 (6): the incoming supply at a glance
    { "Line V",    { VAR_ANALOG_IN, AI_PWR_LN_V } },
    { "Line-line", { VAR_ANALOG_IN, AI_PWR_LL_V } },
    { "Line Hz",   { VAR_ANALOG_IN, AI_PWR_HZ } },
    { "Current",   { VAR_ANALOG_IN, AI_PWR_TOTAL_A } },
    { "Power",     { VAR_ANALOG_IN, AI_PWR_TOTAL_W } },
    { "Pwr factor",{ VAR_ANALOG_IN, AI_PWR_TOTAL_PF } },
    // column 2 (8): the same supply, per phase
    { "L1 V",      { VAR_ANALOG_IN, AI_PWR_L1_V } },
    { "L2 V",      { VAR_ANALOG_IN, AI_PWR_L2_V } },
    { "L3 V",      { VAR_ANALOG_IN, AI_PWR_L3_V } },
    { "L1 A",      { VAR_ANALOG_IN, AI_PWR_L1_A } },
    { "L2 A",      { VAR_ANALOG_IN, AI_PWR_L2_A } },
    { "L3 A",      { VAR_ANALOG_IN, AI_PWR_L3_A } },
    { "Neutral A", { VAR_ANALOG_IN, AI_PWR_N_A } },
    { "THD L-N",   { VAR_ANALOG_IN, AI_PWR_LN_AVG_THD } },
    // column 3 (8): the transfer switch, then derived state
    { "ATS V",     { VAR_ANALOG_IN, AI_ATS_V } },
    { "ATS A",     { VAR_ANALOG_IN, AI_ATS_A } },
    { "ATS W",     { VAR_ANALOG_IN, AI_ATS_W } },
    { "ATS Hz",    { VAR_ANALOG_IN, AI_ATS_HZ } },
    { "Supply Hz", { VAR_INTERNAL_ANALOG,  IA_SYS_SUPPLY_HZ } },
    { "Lockout",   { VAR_INTERNAL_DIGITAL, ID_SYS_SUPPLY_LOCKOUT } },
    { "Screen",    { VAR_DIGITAL_OUT, DO_CLI_SCREEN } },
    { "Skirt",     { VAR_DIGITAL_OUT, DO_CLI_SKIRTVALVE } },
};
const uint8_t g_home_field_count =
    (uint8_t)(sizeof(g_home_fields) / sizeof(g_home_fields[0]));

/** @} */

// Capacity contract: the home screen silently ignores fields past its two
// columns, so pin the count at compile time instead.
static_assert(sizeof(g_home_fields) / sizeof(g_home_fields[0]) <= Config::HOME_FIELDS_MAX,
              "too many home fields: the HomePage would silently drop the tail");

}  // namespace Site

#endif // SITE_STATUS_H
