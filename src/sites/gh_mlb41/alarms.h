#ifndef SITES_GH_MLB41_ALARMS_H
#define SITES_GH_MLB41_ALARMS_H

/**
 * @file alarms.h
 * @brief gh_mlb41 site alarm table (fed to the generic AlarmModel).
 */

#include <cstdint>

#include "libbotanik/alarms/alarms.h"

/** @brief Site PLC alarm table: one entry per PLC alarm, in table (= bit) order.
 *  Read-only (the model copies it into its own working array); PLC bit i maps to
 *  entry i. Defined in alarms.cpp; passed to AlarmModel by the firmware. */
extern const Botanik::Alarms::AlarmCondition g_alarms[];
/** @brief Number of entries in g_alarms (N, the PLC/site alarm count). */
extern const uint8_t g_alarm_count;

#endif // SITES_GH_MLB41_ALARMS_H
