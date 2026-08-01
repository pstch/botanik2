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
 * shell `get` command and MQTT telemetry all resolve these generically
 * against the live variable tables. Each group is published under
 * "<prefix>/telemetry/<group>/<name>", where the leaf is the variable's own
 * name from vars.h - there is no second place to keep names in sync.
 *
 * A row is a VarRef: a bare index into the one variable table. Nothing here
 * carries a byte offset, so re-ordering the table cannot silently re-point a
 * status row at its neighbour; a stale index resolves inertly instead.
 */

#include <cstdint>

#include "botanik.h"

#include "vars.h"

namespace Site {

using namespace Botanik;
using namespace Botanik::Vars;

/** @name Status groups
 *  @{ */

/** @brief Three-phase power meter (SDM630). */
const VarRef g_grp_power[] = {
    { V_PWR_L1_V },
    { V_PWR_L2_V },
    { V_PWR_L3_V },
    { V_PWR_L1_A },
    { V_PWR_L2_A },
    { V_PWR_L3_A },
    { V_PWR_L1_VA },
    { V_PWR_L2_VA },
    { V_PWR_L3_VA },
    { V_PWR_L1_VAR },
    { V_PWR_L2_VAR },
    { V_PWR_L3_VAR },
    { V_PWR_P1_PF },
    { V_PWR_P2_PF },
    { V_PWR_P3_PF },
    { V_PWR_P1_PA },
    { V_PWR_P2_PA },
    { V_PWR_P3_PA },
    { V_PWR_LN_V },
    { V_PWR_LL_V },
    { V_PWR_AVG_A },
    { V_PWR_TOTAL_A },
    { V_PWR_TOTAL_W },
    { V_PWR_TOTAL_VA },
    { V_PWR_TOTAL_VAR },
    { V_PWR_TOTAL_PF },
    { V_PWR_TOTAL_PA },
    { V_PWR_HZ },
    { V_PWR_12_V },
    { V_PWR_L23_V },
    { V_PWR_L31_V },
    { V_PWR_LL_AVG_THD },
    { V_PWR_LN_AVG_THD },
    { V_PWR_N_A },
    { V_PWR_L1_W },
    { V_PWR_L2_W },
    { V_PWR_L3_W },
};

/** @brief Transfer-switch meter (MCT1-140). */
const VarRef g_grp_ats[] = {
    { V_ATS_V },
    { V_ATS_A },
    { V_ATS_W },
    { V_ATS_VAR },
    { V_ATS_VA },
    { V_ATS_HZ },
};

/** @brief Climate actuators: window drives, destratification fans, screen. */
const VarRef g_grp_climate[] = {
    { V_CLI_WINDOW1_OPN },
    { V_CLI_WINDOW1_CLO },
    { V_CLI_WINDOW2_OPN },
    { V_CLI_WINDOW2_CLO },
    { V_CLI_WINDOW3_OPN },
    { V_CLI_WINDOW3_CLO },
    { V_CLI_DESTRAT1_SPEED1 },
    { V_CLI_DESTRAT1_SPEED2 },
    { V_CLI_DESTRAT1_SPEED3 },
    { V_CLI_SCREEN },
    { V_CLI_WINDOW4_OPN },
    { V_CLI_WINDOW4_CLO },
    { V_CLI_DESTRAT2_SPEED1 },
    { V_CLI_DESTRAT2_SPEED2 },
    { V_CLI_DESTRAT2_SPEED3 },
    { V_CLI_SKIRTVALVE },
};

/** @brief Irrigation valves and tray selectors. */
const VarRef g_grp_irrigation[] = {
    { V_IRR_INPUT_RO1 },
    { V_IRR_OUTPUT_RO1 },
    { V_IRR_TRAY1 },
    { V_IRR_TRAY2 },
    { V_IRR_INPUT_RO2 },
    { V_IRR_OUTPUT_RO2 },
    { V_IRR_TRAY3 },
    { V_IRR_TRAY4 },
    { V_IRR_TRAY5 },
    { V_IRR_TRAY6 },
    { V_IRR_TRAY7 },
    { V_IRR_TRAY8 },
};

/** @brief Alarm conditions. Computed digitals, so the alarms page, the monitor
 *  table and telemetry all read the one set of booleans. */
const VarRef g_grp_alarms[] = {
    { V_ALARM_AC_GRID },
    { V_ALARM_DC_PS1 },
    { V_ALARM_GEN_FLT },
    { V_ALARM_AC_LOCAL },
    { V_ALARM_DC_PS2 },
    { V_ALARM_DC_UPS },
    { V_ALARM_DC_LOCAL },
    { V_ALARM_GEN_BATT },
    { V_ALARM_PLC_FLT },
    { V_ALARM_NET_FLT },
    { V_ALARM_NTP_TIM },
    { V_ALARM_ACT_FLT },
    { V_ALARM_SNS_FLT },
    { V_ALARM_TOO_HOT },
    { V_ALARM_TOO_COLD },
    { V_ALARM_TOO_HUM },
    { V_ALARM_TMP_STR },
    { V_ALARM_WTR_LK },
    { V_ALARM_WTR_BAR },
    { V_ALARM_WTR_CLOG },
    { V_ALARM_IO_DROP },
    { V_ALARM_LOG_FLD },
    { V_ALARM_WH_STORM },
};

/** @brief Firmware-derived state. */
const VarRef g_grp_system[] = {
    { V_SYS_SUPPLY_HZ },
    { V_SYS_SUPPLY_LOCKOUT },
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
    { "Line V",    { V_PWR_LN_V } },
    { "Line-line", { V_PWR_LL_V } },
    { "Line Hz",   { V_PWR_HZ } },
    { "Current",   { V_PWR_TOTAL_A } },
    { "Power",     { V_PWR_TOTAL_W } },
    { "Pwr factor",{ V_PWR_TOTAL_PF } },
    // column 2 (8): the same supply, per phase
    { "L1 V",      { V_PWR_L1_V } },
    { "L2 V",      { V_PWR_L2_V } },
    { "L3 V",      { V_PWR_L3_V } },
    { "L1 A",      { V_PWR_L1_A } },
    { "L2 A",      { V_PWR_L2_A } },
    { "L3 A",      { V_PWR_L3_A } },
    { "Neutral A", { V_PWR_N_A } },
    { "THD L-N",   { V_PWR_LN_AVG_THD } },
    // column 3 (8): the transfer switch, then derived state
    { "ATS V",     { V_ATS_V } },
    { "ATS A",     { V_ATS_A } },
    { "ATS W",     { V_ATS_W } },
    { "ATS Hz",    { V_ATS_HZ } },
    { "Supply Hz", { V_SYS_SUPPLY_HZ } },
    { "Lockout",   { V_SYS_SUPPLY_LOCKOUT } },
    { "Screen",    { V_CLI_SCREEN } },
    { "Skirt",     { V_CLI_SKIRTVALVE } },
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
