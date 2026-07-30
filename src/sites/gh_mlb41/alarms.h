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
 * order is load-bearing and must match the payload's alarm booleans. Each row
 * names the byte offset of its own uint8_t boolean in SiteData_t (the model
 * reads it directly; there is no packed bitset).
 *
 * Fields: code, description, offset, then the ON/OFF announce dwell in seconds
 * (-1 disables that announce), the RECENT-mode window, the latch hold after the
 * raw bit clears, the minimum ON time, and an optional remediation note shown on
 * the alarms page. The dynamic suffix is left zeroed for the model to own.
 *
 * min_on_s acts as a local anti-flicker debounce behind the PLC's own.
 */

#include <cstddef>   // offsetof
#include <cstdint>

#include "botanik.h"

#include "data.h"

namespace Site {

// Trailing remediation note is optional, so silence the pedantic
// missing-field-initializer note for the rows that leave it off.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#define AO(f) offsetof(SiteData_t, f)

const Botanik::Alarms::AlarmCondition g_alarms[] = {
    // code        description                        offset             on off rec latch minOn  remediation
    {"AC_GRID",  "Grid AC out-of-spec/offline",       AO(alarm_ac_grid),   2,  2,  0,   0,   10, "check mains breaker / utility feed"},
    {"DC_PS1",   "DC PS1 volts too low",              AO(alarm_dc_ps1),    2,  2,  0,   0,    0},
    {"GEN_FLT",  "Generator failed to start",         AO(alarm_gen_flt),   1,  2,  0,   0,   10, "inspect generator + start battery"},
    {"AC_LOCAL", "Local AC out of spec or offline",   AO(alarm_ac_local),  2,  2,  0,   0,    0},
    {"DC_PS2",   "DC PS2 volts too low",              AO(alarm_dc_ps2),    2,  2,  0,   0,   10},
    {"DC_UPS",   "DC UPS on battery",                 AO(alarm_dc_ups),    1,  2,  0,   0,   10, "restore mains; check UPS battery"},
    {"DC_LOCAL", "Local DC volts too low",            AO(alarm_dc_local),  2,  2,  0,   0,    0},
    {"GEN_BATT", "Gen. start battery low",            AO(alarm_gen_batt),  5,  5,  0,   0,    0},
    {"PLC_FLT",  "Failed to synchronize with PLC",    AO(alarm_plc_flt),   2,  2,  0,   0,    0},
    {"NET_FLT",  "Network error",                     AO(alarm_net_flt),   5,  5,  0,   0,   10},
    {"NTP_TIM",  "NTP sync timeout",                  AO(alarm_ntp_tim),   5,  5,  0,   0,   10},
    {"ACT_FLT",  "Actioner failure",                  AO(alarm_act_flt),   2,  2,  0,   0,    0},
    {"SNS_FLT",  "Sensor fault",                      AO(alarm_sns_flt),   2,  2,  0,   0,    0},
    {"TOO_HOT",  "Inside temperature too high",       AO(alarm_too_hot),   3,  3,  0,   0,   15, "increase ventilation / open windows"},
    {"TOO_COLD", "Inside temperature too low",        AO(alarm_too_cold),  3,  3,  0,   0,   15},
    {"TOO_HUM",  "Relative humidity too high",        AO(alarm_too_hum),   3,  3,  0,   0,   15},
    {"TMP_STR",  "Temp. stratification too high",     AO(alarm_tmp_str),   5,  5,  0,   0,    0},
    {"WTR_LK",   "Leak in irrigation circuit",        AO(alarm_wtr_lk),    1,  2,  0,   0,    0, "shut irrigation valve, inspect circuit"},
    {"WTR_BAR",  "Water pressure too low",            AO(alarm_wtr_bar),   2,  2,  0,   0,    0},
    {"WTR_CLOG", "Failing to drain a tray",           AO(alarm_wtr_clog),  3,  3,  0,   0,    0},
    {"IO_DROP",  "Logical I/O drops",                 AO(alarm_io_drop),   2,  2,  0,   0,    0},
    {"LOG_FLD",  "Logs flooding",                     AO(alarm_log_fld),   2,  2,  0,   0,    0},
    {"WH_STORM", "Current storm conditions",          AO(alarm_wh_storm),  2,  5,  0,   0,   15, "close roof windows, secure screens"},
};

#undef AO
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
