#ifndef ALARMS_PLATFORM_H
#define ALARMS_PLATFORM_H

/**
 * @file platform.h
 * @brief Generic platform-health alarm table - the alarms BotanikFirmware::loop()
 *        raises from the scheduler metrics (reset, CPU load/jitter, stack, overrun).
 *
 * Site-independent. AlarmModel appends them at [N..N+M) (after the site's N PLC
 * alarms), in a disjoint namespace, so local platform bits and PLC bits never collide.
 */

#include "libbotanik/alarms/alarms.h"

namespace Botanik { namespace Alarms {

    /** @brief The generic platform-health alarms, by code (appended after site alarms). */
    extern const AlarmCondition PLATFORM_ALARMS[];
    /** @brief Number of entries in PLATFORM_ALARMS. */
    extern const uint8_t        PLATFORM_ALARM_COUNT;

}}

#endif // ALARMS_PLATFORM_H
