#ifndef SITES_GH_MLB41_ALARMS_H
#define SITES_GH_MLB41_ALARMS_H

/** @file alarms.h
 *  @brief gh_mlb41 site alarm table (fed to the generic AlarmModel). */

#include <cstdint>

#include "libbotanik/alarms/alarms.h"

// One entry per alarm, in table (= bit) order. Static prefix filled here (code,
// description, masked_by, ON/OFF log+mqtt dwell thresholds in seconds; -1
// disables an action); dynamic suffix left zeroed for the model. Trailing
// recent_s/latch_s/min_on_s (RECENT-mode window; latch hold after the raw bit
// clears; minimum ON time) default to 0 when omitted. min_on_s on the
// mock-animated rows is a local anti-flicker debounce behind the PLC's own.
// masked_by is 0 for now. Header-only (const = internal linkage): the single
// site-consuming TU per binary gets the table.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
const Botanik::Alarms::AlarmCondition g_alarms[] = {
    // code        description                              mask  logOn logOff mqttOn mqttOff recent latch minOn
    {"AC_GRID",  "Grid AC out-of-spec/offline",            0,    2,    2,     15,    15,      0,     0,    10},
    {"DC_PS1",   "DC PS1 volts too low",                   0,    2,    2,     15,    15},
    {"GEN_FLT",  "Generator failed to start",              0,    1,    2,     10,    15,      0,     0,    10},
    {"AC_LOCAL", "Local AC out of spec or offline",        0,    2,    2,     15,    15},
    {"DC_PS2",   "DC PS2 volts too low",                   0,    2,    2,     15,    15,      0,     0,    10},
    {"DC_UPS",   "DC UPS on battery",                      0,    1,    2,     10,    15,      0,     0,    10},
    {"DC_LOCAL", "Local DC volts too low",                 0,    2,    2,     15,    15},
    {"GEN_BATT", "Gen. start battery low",                 0,    5,    5,     30,    30},
    {"PLC_FLT",  "Failed to synchronize with PLC",         0,    2,    2,     10,    15},
    {"NET_FLT",  "Network error",                          0,    5,    5,     30,    30,      0,     0,    10},
    {"NTP_TIM",  "NTP sync timeout",                       0,    5,    5,     -1,    -1,      0,     0,    10},
    {"ACT_FLT",  "Actioner failure",                       0,    2,    2,     15,    15},
    {"SNS_FLT",  "Sensor fault",                           0,    2,    2,     15,    15},
    {"TOO_HOT",  "Inside temperature too high",            0,    3,    3,     20,    20,      0,     0,    15},
    {"TOO_COLD", "Inside temperature too low",             0,    3,    3,     20,    20,      0,     0,    15},
    {"TOO_HUM",  "Relative humidity too high",             0,    3,    3,     20,    20,      0,     0,    15},
    {"TMP_STR",  "Temp. stratification too high",          0,    5,    5,     -1,    -1},
    {"WTR_LK",   "Leak in irrigation circuit",             0,    1,    2,     10,    15},
    {"WTR_BAR",  "Water pressure too low",                 0,    2,    2,     15,    15},
    {"WTR_CLOG", "Failing to drain a tray",                0,    3,    3,     20,    20},
    {"IO_DROP",  "Logical I/O drops",                      0,    2,    2,     -1,    -1},
    {"LOG_FLD",  "Logs flooding",                          0,    2,    2,     -1,    -1},
    {"WH_STORM", "Current storm conditions",               0,    2,    5,     15,    30,      0,     0,    15},
};
const uint8_t g_alarm_count = (uint8_t)(sizeof(g_alarms) / sizeof(g_alarms[0]));
#pragma GCC diagnostic pop

#endif // SITES_GH_MLB41_ALARMS_H
