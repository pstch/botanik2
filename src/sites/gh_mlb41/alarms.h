// REVIEW_35: TODO
#ifndef SITES_GH_MLB41_ALARMS_H
#define SITES_GH_MLB41_ALARMS_H

/** @file alarms.h
 *  @brief gh_mlb41 site alarm table (fed to the generic AlarmModel). */

#include <cstddef>   // offsetof (each alarm's payload boolean)
#include <cstdint>

#include "libbotanik/alarms/alarms.h"

// One entry per alarm, in table order (row index = the display slot). Static
// prefix filled here: code, description, the byte offset of this alarm's boolean
// field in PlcOutputData_t (the model reads it directly), then the ON/OFF
// announce dwell thresholds in seconds (-1 disables an announce), the RECENT-mode
// window, the latch hold after the raw bit clears, and the minimum ON time. The
// dynamic suffix is left zeroed for the model. min_on_s on the mock-animated rows
// is a local anti-flicker debounce behind the PLC's own. Included from site.h
// after PlcOutputData_t, so offsetof resolves; header-only (const = internal
// linkage), so the single site-consuming TU per binary gets the table.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
const Botanik::Alarms::AlarmCondition g_alarms[] = {
    // code        description                        offset                                       on off rec latch minOn
    {"AC_GRID",  "Grid AC out-of-spec/offline",       offsetof(PlcOutputData_t, alarm_ac_grid),   2,  2,  0,   0,   10},
    {"DC_PS1",   "DC PS1 volts too low",              offsetof(PlcOutputData_t, alarm_dc_ps1),    2,  2,  0,   0,    0},
    {"GEN_FLT",  "Generator failed to start",         offsetof(PlcOutputData_t, alarm_gen_flt),   1,  2,  0,   0,   10},
    {"AC_LOCAL", "Local AC out of spec or offline",   offsetof(PlcOutputData_t, alarm_ac_local),  2,  2,  0,   0,    0},
    {"DC_PS2",   "DC PS2 volts too low",              offsetof(PlcOutputData_t, alarm_dc_ps2),    2,  2,  0,   0,   10},
    {"DC_UPS",   "DC UPS on battery",                 offsetof(PlcOutputData_t, alarm_dc_ups),    1,  2,  0,   0,   10},
    {"DC_LOCAL", "Local DC volts too low",            offsetof(PlcOutputData_t, alarm_dc_local),  2,  2,  0,   0,    0},
    {"GEN_BATT", "Gen. start battery low",            offsetof(PlcOutputData_t, alarm_gen_batt),  5,  5,  0,   0,    0},
    {"PLC_FLT",  "Failed to synchronize with PLC",    offsetof(PlcOutputData_t, alarm_plc_flt),   2,  2,  0,   0,    0},
    {"NET_FLT",  "Network error",                     offsetof(PlcOutputData_t, alarm_net_flt),   5,  5,  0,   0,   10},
    {"NTP_TIM",  "NTP sync timeout",                  offsetof(PlcOutputData_t, alarm_ntp_tim),   5,  5,  0,   0,   10},
    {"ACT_FLT",  "Actioner failure",                  offsetof(PlcOutputData_t, alarm_act_flt),   2,  2,  0,   0,    0},
    {"SNS_FLT",  "Sensor fault",                      offsetof(PlcOutputData_t, alarm_sns_flt),   2,  2,  0,   0,    0},
    {"TOO_HOT",  "Inside temperature too high",       offsetof(PlcOutputData_t, alarm_too_hot),   3,  3,  0,   0,   15},
    {"TOO_COLD", "Inside temperature too low",        offsetof(PlcOutputData_t, alarm_too_cold),  3,  3,  0,   0,   15},
    {"TOO_HUM",  "Relative humidity too high",        offsetof(PlcOutputData_t, alarm_too_hum),   3,  3,  0,   0,   15},
    {"TMP_STR",  "Temp. stratification too high",     offsetof(PlcOutputData_t, alarm_tmp_str),   5,  5,  0,   0,    0},
    {"WTR_LK",   "Leak in irrigation circuit",        offsetof(PlcOutputData_t, alarm_wtr_lk),    1,  2,  0,   0,    0},
    {"WTR_BAR",  "Water pressure too low",            offsetof(PlcOutputData_t, alarm_wtr_bar),   2,  2,  0,   0,    0},
    {"WTR_CLOG", "Failing to drain a tray",           offsetof(PlcOutputData_t, alarm_wtr_clog),  3,  3,  0,   0,    0},
    {"IO_DROP",  "Logical I/O drops",                 offsetof(PlcOutputData_t, alarm_io_drop),   2,  2,  0,   0,    0},
    {"LOG_FLD",  "Logs flooding",                     offsetof(PlcOutputData_t, alarm_log_fld),   2,  2,  0,   0,    0},
    {"WH_STORM", "Current storm conditions",          offsetof(PlcOutputData_t, alarm_wh_storm),  2,  5,  0,   0,   15},
};
const uint8_t g_alarm_count = (uint8_t)(sizeof(g_alarms) / sizeof(g_alarms[0]));
#pragma GCC diagnostic pop

#endif // SITES_GH_MLB41_ALARMS_H
