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
    {"ac_grid_flt",  "Grid AC out-of-spec/offline",       {V_AC_GRID_FLT},   2,  2,  0,   0,   10, "check mains breaker / utility feed"},
    {"dc_ps1_flt",   "DC PS1 volts too low",              {V_DC_PS1_FLT},    2,  2,  0,   0,    0},
    {"gen_flt",  "Generator failed to start",         {V_GEN_FLT},   1,  2,  0,   0,   10, "inspect generator + start battery"},
    {"ac_local_flt", "Local AC out of spec or offline",   {V_AC_LOCAL_FLT},  2,  2,  0,   0,    0},
    {"dc_ps2_flt",   "DC PS2 volts too low",              {V_DC_PS2_FLT},    2,  2,  0,   0,   10},
    {"dc_ups_flt",   "DC UPS on battery",                 {V_DC_UPS_FLT},    1,  2,  0,   0,   10, "restore mains; check UPS battery"},
    {"dc_local_flt", "Local DC volts too low",            {V_DC_LOCAL_FLT},  2,  2,  0,   0,    0},
    {"gen_batt_flt", "Gen. start battery low",            {V_GEN_BATT_FLT},  5,  5,  0,   0,    0},
    {"plc_flt",  "Failed to synchronize with PLC",    {V_PLC_FLT},   2,  2,  0,   0,    0},
    {"net_flt",  "Network error",                     {V_NET_FLT},   5,  5,  0,   0,   10},
    {"ntp_tim_flt",  "NTP sync timeout",                  {V_NTP_TIM_FLT},   5,  5,  0,   0,   10},
    {"act_flt",  "Actioner failure",                  {V_ACT_FLT},   2,  2,  0,   0,    0},
    {"sns_flt",  "Sensor fault",                      {V_SNS_FLT},   2,  2,  0,   0,    0},
    {"too_hot_flt",  "Inside temperature too high",       {V_TOO_HOT_FLT},   3,  3,  0,   0,   15, "increase ventilation / open windows"},
    {"too_cold_flt", "Inside temperature too low",        {V_TOO_COLD_FLT},  3,  3,  0,   0,   15},
    {"too_hum_flt",  "Relative humidity too high",        {V_TOO_HUM_FLT},   3,  3,  0,   0,   15},
    {"tmp_str_flt",  "Temp. stratification too high",     {V_TMP_STR_FLT},   5,  5,  0,   0,    0},
    {"wtr_lk_flt",   "Leak in irrigation circuit",        {V_WTR_LK_FLT},    1,  2,  0,   0,    0, "shut irrigation valve, inspect circuit"},
    {"wtr_bar_flt",  "Water pressure too low",            {V_WTR_BAR_FLT},   2,  2,  0,   0,    0},
    {"wtr_clog_flt", "Failing to drain a tray",           {V_WTR_CLOG_FLT},  3,  3,  0,   0,    0},
    {"io_drop_flt",  "Logical I/O drops",                 {V_IO_DROP_FLT},   2,  2,  0,   0,    0},
    {"log_fld_flt",  "Logs flooding",                     {V_LOG_FLD_FLT},   2,  2,  0,   0,    0},
    {"wh_storm_flt", "Current storm conditions",          {V_WH_STORM_FLT},  2,  5,  0,   0,   15, "close roof windows, secure screens"},
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
