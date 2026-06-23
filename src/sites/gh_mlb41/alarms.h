#ifndef SITES_GH_MLB41_ALARMS_H
#define SITES_GH_MLB41_ALARMS_H

/** @file alarms.h
 *  @brief gh_mlb41 site alarm table (fed to the generic AlarmModel). */

#include <cstdint>

#include "libbotanik/alarms/alarms.h"

extern const Botanik::Alarms::AlarmCondition g_alarms[];
extern const uint8_t g_alarm_count;   ///< N, the PLC/site alarm count

#endif // SITES_GH_MLB41_ALARMS_H
