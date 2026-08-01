#ifndef SITE_ALARMS_H
#define SITE_ALARMS_H

/**
 * @file alarms.h
 * @brief GENERATED - the gh_mlb41 alarm conditions, fed to the generic
 *        AlarmModel.
 *
 * Generated from the Arduino PLC IDE project. Regenerating overwrites this
 * file.
 *
 * One row per alarm, in table order: the ROW INDEX IS THE ALARM BIT, so row
 * order is load-bearing and must match the internal block of vars.h,
 * from which these rows are generated together. Each row names the variable
 * carrying its condition (the model reads it directly; there is no packed
 * bitset).
 *
 * Fields: code, description, variable, then the ON/OFF announce dwell in seconds
 * (-1 disables that announce), the RECENT-mode window, the latch hold after the
 * raw bit clears, the minimum ON time, and an optional remediation note shown on
 * the alarms page. The dynamic suffix is left zeroed for the model to own.
 *
 * min_on_s acts as a local anti-flicker debounce behind the PLC's own.
 */

#include <cstdint>

#include "botanik.h"

#include "vars.h"

namespace Site {

using namespace Botanik::Vars;

// Trailing remediation note is optional, so silence the pedantic
// missing-field-initializer note for the rows that leave it off.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

const Botanik::Alarms::AlarmCondition g_alarms[] = {
    // code        description                        variable                                on off rec latch minOn  remediation
    {"alarm_ac_grid",  "Grid AC out-of-spec/offline",       {V_ALARM_AC_GRID},   2,  2,  0,   0,   10, "check mains breaker / utility feed"},
    {"alarm_dc_ps1",   "DC PS1 volts too low",              {V_ALARM_DC_PS1},    2,  2,  0,   0,    0},
    {"alarm_gen",  "Generator failed to start",         {V_ALARM_GEN},   1,  2,  0,   0,   10, "inspect generator + start battery"},
    {"alarm_ac_local", "Local AC out of spec or offline",   {V_ALARM_AC_LOCAL},  2,  2,  0,   0,    0},
    {"alarm_dc_ps2",   "DC PS2 volts too low",              {V_ALARM_DC_PS2},    2,  2,  0,   0,   10},
    {"alarm_dc_ups",   "DC UPS on battery",                 {V_ALARM_DC_UPS},    1,  2,  0,   0,   10, "restore mains; check UPS battery"},
    {"alarm_dc_local", "Local DC volts too low",            {V_ALARM_DC_LOCAL},  2,  2,  0,   0,    0},
    {"alarm_gen_batt", "Gen. start battery low",            {V_ALARM_GEN_BATT},  5,  5,  0,   0,    0},
    {"alarm_plc",  "Failed to synchronize with PLC",    {V_ALARM_PLC},   2,  2,  0,   0,    0},
    {"alarm_net",  "Network error",                     {V_ALARM_NET},   5,  5,  0,   0,   10},
    {"alarm_ntp_tim",  "NTP sync timeout",                  {V_ALARM_NTP_TIM},   5,  5,  0,   0,   10},
    {"alarm_act",  "Actioner failure",                  {V_ALARM_ACT},   2,  2,  0,   0,    0},
    {"alarm_sns",  "Sensor fault",                      {V_ALARM_SNS},   2,  2,  0,   0,    0},
    {"alarm_too_hot",  "Inside temperature too high",       {V_ALARM_TOO_HOT},   3,  3,  0,   0,   15, "increase ventilation / open windows"},
    {"alarm_too_cold", "Inside temperature too low",        {V_ALARM_TOO_COLD},  3,  3,  0,   0,   15},
    {"alarm_too_hum",  "Relative humidity too high",        {V_ALARM_TOO_HUM},   3,  3,  0,   0,   15},
    {"alarm_tmp_str",  "Temp. stratification too high",     {V_ALARM_TMP_STR},   5,  5,  0,   0,    0},
    {"alarm_wtr_lk",   "Leak in irrigation circuit",        {V_ALARM_WTR_LK},    1,  2,  0,   0,    0, "shut irrigation valve, inspect circuit"},
    {"alarm_wtr_bar",  "Water pressure too low",            {V_ALARM_WTR_BAR},   2,  2,  0,   0,    0},
    {"alarm_wtr_clog", "Failing to drain a tray",           {V_ALARM_WTR_CLOG},  3,  3,  0,   0,    0},
    {"alarm_io_drop",  "Logical I/O drops",                 {V_ALARM_IO_DROP},   2,  2,  0,   0,    0},
    {"alarm_log_fld",  "Logs flooding",                     {V_ALARM_LOG_FLD},   2,  2,  0,   0,    0},
    {"alarm_wh_storm", "Current storm conditions",          {V_ALARM_WH_STORM},  2,  5,  0,   0,   15, "close roof windows, secure screens"},
};

#pragma GCC diagnostic pop

const uint8_t g_alarm_count = (uint8_t)(sizeof(g_alarms) / sizeof(g_alarms[0]));

// Capacity contract: the model's working array is shared with the generic
// platform-health alarms it splices in above ours, and it CLAMPS a too-long site
// table - silently dropping the tail, which would then never fire. Fail the
// build instead.
static_assert(sizeof(g_alarms) / sizeof(g_alarms[0]) <= Botanik::Config::SITE_ALARMS_MAX,
              "too many site alarms: the AlarmModel would clamp and drop the tail");

}  // namespace Site

#endif // SITE_ALARMS_H
