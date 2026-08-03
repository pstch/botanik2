#ifndef SCAN_BLOCKS_COMMON_H
#define SCAN_BLOCKS_COMMON_H

/**
 * @file common.h
 * @brief The handful of helpers every function block shares.
 *
 * Saturating time accumulation, a float clamp, and the ms -> s conversion.
 * Seconds are the unit every gain, rate and time constant in these blocks is
 * expressed in, so the conversion happens once, here, rather than as a scatter
 * of `/ 1000.0f` in each block.
 */

#include <cstdint>

namespace Botanik { namespace Scan {

    /** @name Helpers
     *  @{ */

    /** @brief Advance @p elapsed by @p dt_ms, saturating at @p limit. Saturating
     *  rather than wrapping matters: a hold far longer than the limit must stay
     *  elapsed, not roll back around to zero. Shared by every timer. */
    void accumulate(uint32_t& elapsed, uint32_t limit, uint32_t dt_ms);

    /** @brief Count @p dt_ms down from @p remaining, stopping at zero. The
     *  mirror of accumulate() for blocks that hold a time budget. */
    void countdown(uint32_t& remaining, uint32_t dt_ms);

    /** @brief @p v held inside [@p lo, @p hi]. */
    float clampf(float v, float lo, float hi);

    /** @brief @p dt_ms as seconds. */
    float seconds(uint32_t dt_ms);

    /** @} */

}}  // namespace Botanik::Scan

#endif // SCAN_BLOCKS_COMMON_H
