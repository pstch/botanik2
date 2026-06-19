#ifndef SITES_GH_MLB41_ALARMS_H
#define SITES_GH_MLB41_ALARMS_H

/**
 * @file alarms.h
 * @brief gh_mlb41 site alarm table (fed to the generic AlarmModel).
 */

#include "libbotanik/alarms/alarms.h"

/** @brief Site alarm table: one entry per alarm, in table (= bit) order.
 *  Defined in alarms.cpp; passed to AlarmModel by the firmware. */
extern Botanik::Alarms::AlarmCondition g_alarms[Botanik::Alarms::ALARM_BITS];

#endif // SITES_GH_MLB41_ALARMS_H
