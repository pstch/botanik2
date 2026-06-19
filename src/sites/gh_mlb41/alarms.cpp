/**
 * @file alarms.cpp
 * @brief gh_mlb41 site alarm table definition (see alarms.h).
 */

#include "sites/gh_mlb41/alarms.h"

// One entry per alarm, in table (= bit) order. The static prefix is filled
// here (code, description, masked_by, ON/OFF dwell thresholds in seconds; -1
// disables an action); the dynamic suffix is left zeroed for the model. Two trailing
// fields follow the dwell thresholds: recent_s (RECENT-mode window; 0 = one-week
// default) and latch_s (hold active this long after the raw bit clears; 0 = no latch).
// Both are omitted on most rows (so they default to 0). masked_by is 0 for now.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
const Botanik::Alarms::AlarmCondition g_alarms[] = {
    // code        description                                      mask  logOn logOff mqttOn mqttOff
    {"AC_GRID",  "Grid AC out-of-spec/offline",                     0,    2,    2,     15,    15},
    {"DC_PS1",   "DC PS1 volts too low",                            0,    2,    2,     15,    15},
    {"GEN_FLT",  "Generator failed to start",                       0,    1,    2,     10,    15},
    {"AC_LOCAL", "Local AC out of spec or offline",                 0,    2,    2,     15,    15},
    {"DC_PS2",   "DC PS2 volts too low",                            0,    2,    2,     15,    15},
    {"DC_UPS",   "DC UPS on battery",                               0,    1,    2,     10,    15},
    {"DC_LOCAL", "Local DC volts too low",                          0,    2,    2,     15,    15},
    {"GEN_BATT", "Gen. start battery low",                          0,    5,    5,     30,    30},
    {"PLC_FLT",  "Failed to synchronize with PLC",                  0,    2,    2,     10,    15},
    {"NET_FLT",  "Network error",                                   0,    5,    5,     30,    30},
    {"NTP_TIM",  "NTP sync timeout",                                0,    5,    5,     -1,    -1},
    {"ACT_FLT",  "Actioner failure",                                0,    2,    2,     15,    15},
    {"SNS_FLT",  "Sensor fault",                                    0,    2,    2,     15,    15},
    {"TOO_HOT",  "Inside temperature too high",                     0,    3,    3,     20,    20},
    {"TOO_COLD", "Inside temperature too low",                      0,    3,    3,     20,    20},
    {"TOO_HUM",  "Relative humidity too high",                      0,    3,    3,     20,    20},
    {"TMP_STR",  "Temp. stratification too high",                   0,    5,    5,     -1,    -1},
    {"WTR_LK",   "Leak in irrigation circuit",                      0,    1,    2,     10,    15},
    {"WTR_BAR",  "Water pressure too low",                          0,    2,    2,     15,    15},
    {"WTR_CLOG", "Failing to drain a tray",                         0,    3,    3,     20,    20},
    {"IO_DROP",  "Logical I/O drops",                               0,    2,    2,     -1,    -1},
    {"LOG_FLD",  "Logs flooding",                                   0,    2,    2,     -1,    -1},
    {"WH_STORM", "Current storm conditions",                        0,    2,    5,     15,    30},
    // Generic platform-health alarms (SYS_RST, CPU_*, STACK) are NOT here: they live
    // in the base table (libbotanik/alarms/platform.h) and AlarmModel appends them
    // after these site alarms. This table is site/PLC alarms only.
};
const uint8_t g_alarm_count = (uint8_t)(sizeof(g_alarms) / sizeof(g_alarms[0]));
#pragma GCC diagnostic pop
