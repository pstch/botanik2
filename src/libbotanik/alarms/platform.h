#ifndef ALARMS_PLATFORM_H
#define ALARMS_PLATFORM_H

/**
 * @file platform.h
 * @brief Generic platform-health alarm table - the alarms BotanikFirmware::loop()
 *        raises from the scheduler metrics (reset, CPU load/jitter, stack, overrun).
 *
 * Site-independent, so they live here rather than in each site's table. AlarmModel
 * splices them into the *top* of the 64-bit bitset, away from the site/PLC alarms
 * that fill from bit 0, so the locally-set platform bits and the PLC-reported site
 * bits can never collide.
 */

#include "libbotanik/alarms/alarms.h"

namespace Botanik { namespace Alarms {

    /** @brief The generic platform-health alarms, by code, in high-bit order. */
    extern const AlarmCondition PLATFORM_ALARMS[];
    /** @brief Number of entries in PLATFORM_ALARMS. */
    extern const uint8_t        PLATFORM_ALARM_COUNT;

}}

#endif // ALARMS_PLATFORM_H
