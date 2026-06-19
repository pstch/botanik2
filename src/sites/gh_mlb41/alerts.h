#ifndef SITES_GH_MLB41_ALERTS_H
#define SITES_GH_MLB41_ALERTS_H

/**
 * @file alerts.h
 * @brief gh_mlb41 site alert table (fed to the generic AlertModel).
 */

#include "libbotanik/alerts/alerts.h"

/** @brief Site alert table: one entry per Alerts::AlertId, in bit-index order.
 *  Defined in alerts.cpp; passed to AlertModel by the firmware. */
extern Botanik::Alerts::AlertCondition g_alerts[Botanik::Alerts::ALERT_BITS];

#endif // SITES_GH_MLB41_ALERTS_H
