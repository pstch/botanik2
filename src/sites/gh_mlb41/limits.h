#ifndef SITE_LIMITS_H
#define SITE_LIMITS_H

/**
 * @file limits.h
 * @brief HAND-WRITTEN - the constants more than one program needs.
 *
 * A program's own numbers stay in its own .cpp. This is for the few that two
 * laws have to agree on, which would otherwise make one program include
 * another.
 */

namespace Site {

/** @brief How close to the dew point counts as condensing, in degC. The rot
 *  alarm is raised against it and the humidity guard is floored at it, so the
 *  two cannot disagree about what "wet" means. */
static const float ROT_MARGIN_DEGC = 1.0f;

}  // namespace Site

#endif // SITE_LIMITS_H
