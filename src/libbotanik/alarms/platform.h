// REVIEW_35: TODO
#ifndef ALARMS_PLATFORM_H
#define ALARMS_PLATFORM_H

/**
 * @file platform.h
 * @brief Generic platform-health alarm table.
 *
 * Defines the platform-level alarms, raised from the scheduler metrics.
 * CPU load/jitter, stack, overrun).
 *
 * The site-specific AlarmModel appends them at [N..N+M) (after the site's
 * N alarms), in a disjoint namespace, so platform bits and site bits never
 * collide.
 */

#include "alarms.h"

namespace Botanik { namespace Alarms {

    /** @brief Generic platform-health alarms, by code (appended after site). */
    extern const AlarmCondition PLATFORM_ALARMS[];
    /** @brief Number of entries in PLATFORM_ALARMS. */
    extern const uint8_t        PLATFORM_ALARM_COUNT;

}}

#endif // ALARMS_PLATFORM_H
