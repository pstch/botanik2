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
 * order is load-bearing and must match the internal-digital table in io.h,
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

#include "io.h"

namespace Site {

using namespace Botanik::Vars;

// Trailing remediation note is optional, so silence the pedantic
// missing-field-initializer note for the rows that leave it off.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

const Botanik::Alarms::AlarmCondition g_alarms[] = {
    // code        description                        variable                                on off rec latch minOn  remediation
    {"AC_GRID",  "Grid AC out-of-spec/offline",       {VAR_INTERNAL_DIGITAL, ID_AC_GRID},   2,  2,  0,   0,   10, "check mains breaker / utility feed"},
    {"DC_PS1",   "DC PS1 volts too low",              {VAR_INTERNAL_DIGITAL, ID_DC_PS1},    2,  2,  0,   0,    0},
    {"GEN_FLT",  "Generator failed to start",         {VAR_INTERNAL_DIGITAL, ID_GEN_FLT},   1,  2,  0,   0,   10, "inspect generator + start battery"},
    {"AC_LOCAL", "Local AC out of spec or offline",   {VAR_INTERNAL_DIGITAL, ID_AC_LOCAL},  2,  2,  0,   0,    0},
    {"DC_PS2",   "DC PS2 volts too low",              {VAR_INTERNAL_DIGITAL, ID_DC_PS2},    2,  2,  0,   0,   10},
    {"DC_UPS",   "DC UPS on battery",                 {VAR_INTERNAL_DIGITAL, ID_DC_UPS},    1,  2,  0,   0,   10, "restore mains; check UPS battery"},
    {"DC_LOCAL", "Local DC volts too low",            {VAR_INTERNAL_DIGITAL, ID_DC_LOCAL},  2,  2,  0,   0,    0},
    {"GEN_BATT", "Gen. start battery low",            {VAR_INTERNAL_DIGITAL, ID_GEN_BATT},  5,  5,  0,   0,    0},
    {"PLC_FLT",  "Failed to synchronize with PLC",    {VAR_INTERNAL_DIGITAL, ID_PLC_FLT},   2,  2,  0,   0,    0},
    {"NET_FLT",  "Network error",                     {VAR_INTERNAL_DIGITAL, ID_NET_FLT},   5,  5,  0,   0,   10},
    {"NTP_TIM",  "NTP sync timeout",                  {VAR_INTERNAL_DIGITAL, ID_NTP_TIM},   5,  5,  0,   0,   10},
    {"ACT_FLT",  "Actioner failure",                  {VAR_INTERNAL_DIGITAL, ID_ACT_FLT},   2,  2,  0,   0,    0},
    {"SNS_FLT",  "Sensor fault",                      {VAR_INTERNAL_DIGITAL, ID_SNS_FLT},   2,  2,  0,   0,    0},
    {"TOO_HOT",  "Inside temperature too high",       {VAR_INTERNAL_DIGITAL, ID_TOO_HOT},   3,  3,  0,   0,   15, "increase ventilation / open windows"},
    {"TOO_COLD", "Inside temperature too low",        {VAR_INTERNAL_DIGITAL, ID_TOO_COLD},  3,  3,  0,   0,   15},
    {"TOO_HUM",  "Relative humidity too high",        {VAR_INTERNAL_DIGITAL, ID_TOO_HUM},   3,  3,  0,   0,   15},
    {"TMP_STR",  "Temp. stratification too high",     {VAR_INTERNAL_DIGITAL, ID_TMP_STR},   5,  5,  0,   0,    0},
    {"WTR_LK",   "Leak in irrigation circuit",        {VAR_INTERNAL_DIGITAL, ID_WTR_LK},    1,  2,  0,   0,    0, "shut irrigation valve, inspect circuit"},
    {"WTR_BAR",  "Water pressure too low",            {VAR_INTERNAL_DIGITAL, ID_WTR_BAR},   2,  2,  0,   0,    0},
    {"WTR_CLOG", "Failing to drain a tray",           {VAR_INTERNAL_DIGITAL, ID_WTR_CLOG},  3,  3,  0,   0,    0},
    {"IO_DROP",  "Logical I/O drops",                 {VAR_INTERNAL_DIGITAL, ID_IO_DROP},   2,  2,  0,   0,    0},
    {"LOG_FLD",  "Logs flooding",                     {VAR_INTERNAL_DIGITAL, ID_LOG_FLD},   2,  2,  0,   0,    0},
    {"WH_STORM", "Current storm conditions",          {VAR_INTERNAL_DIGITAL, ID_WH_STORM},  2,  5,  0,   0,   15, "close roof windows, secure screens"},
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
