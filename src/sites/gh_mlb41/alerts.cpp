/**
 * @file alerts.cpp
 * @brief gh_mlb41 site alert table definition (see alerts.h).
 */

#include "sites/gh_mlb41/alerts.h"

// One entry per Alerts::AlertId, in bit-index order. The static prefix is filled
// here (code, description, masked_by, ON/OFF dwell thresholds in ms; -1 disables
// an action); the dynamic suffix is left zeroed for the model. masked_by is 0 for
// now (no masking relationships defined yet); the mechanism is in place for the
// site to fill. Thresholds are placeholders to be tuned per site.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
Botanik::Alerts::AlertCondition g_alerts[Botanik::Alerts::ALERT_BITS] = {
    // code        description                                      mask  logOn  logOff mqttOn mqttOff
    {"AC_GRID",  "Grid AC out of spec or offline",                  0,    2000,  2000,  15000, 15000},
    {"DC_PS1",   "DC PS1 volts too low",                            0,    2000,  2000,  15000, 15000},
    {"GEN_FLT",  "Generator failed to start",                       0,    1000,  2000,  10000, 15000},
    {"AC_LOCAL", "Local AC out of spec or offline",                 0,    2000,  2000,  15000, 15000},
    {"DC_PS2",   "DC PS2 volts too low",                            0,    2000,  2000,  15000, 15000},
    {"DC_UPS",   "DC UPS on battery",                               0,    1000,  2000,  10000, 15000},
    {"DC_LOCAL", "Local DC volts too low",                          0,    2000,  2000,  15000, 15000},
    {"GEN_BATT", "Generator start battery too low or offline",      0,    5000,  5000,  30000, 30000},
    {"PLC_FLT",  "Failed to synchronize with PLC",                  0,    2000,  2000,  10000, 15000},
    {"NET_FLT",  "Network should be set up but no TCP/IP I/O",      0,    5000,  5000,  30000, 30000},
    {"NTP_TIM",  "Timeout while attempting NTP sync",               0,    5000,  5000,  -1,    -1},
    {"ACT_FLT",  "An actioner faulted in the last hour",            0,    2000,  2000,  15000, 15000},
    {"SNS_FLT",  "A sensor faulted in the last hour",               0,    2000,  2000,  15000, 15000},
    {"TOO_HOT",  "Inside temperature too high",                     0,    3000,  3000,  20000, 20000},
    {"TOO_COLD", "Inside temperature too low",                      0,    3000,  3000,  20000, 20000},
    {"TOO_HUM",  "Relative humidity too high",                      0,    3000,  3000,  20000, 20000},
    {"TMP_STR",  "Temperature stratification too high",             0,    5000,  5000,  -1,    -1},
    {"SYS_RST",  "Reinitialized in the last five minutes",          0,    0,     -1,    -1,    -1},
    {"WTR_LK",   "Leak in irrigation circuit",                      0,    1000,  2000,  10000, 15000},
    {"WTR_BAR",  "Water pressure too low",                          0,    2000,  2000,  15000, 15000},
    {"WTR_CLOG", "Failing to drain a tray",                         0,    3000,  3000,  20000, 20000},
    {"IO_DROP",  "Logical I/O drops",                               0,    2000,  2000,  -1,    -1},
    {"LOG_FLD",  "Logs flooding",                                   0,    2000,  2000,  -1,    -1},
    {"WH_STORM", "Current storm conditions",                        0,    2000,  5000,  15000, 30000},
};
#pragma GCC diagnostic pop
