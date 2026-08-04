#ifndef PLC_FUNCTIONS_TIME_H
#define PLC_FUNCTIONS_TIME_H

/**
 * @file time.h
 * @brief Time arithmetic on a caller-held counter: the seconds conversion, and
 *        the two saturating steps every timed block runs on.
 *
 * No clock is read here, and none is held. A block passes its own counter in;
 * these say what one step of @c dt_ms does to it. That is what keeps the blocks
 * testable without a clock, and it is why these count as FUNCTIONs despite
 * taking a reference: the state belongs to the caller (IEC's VAR_IN_OUT), not
 * to anything here.
 */

#include <cstdint>

namespace Botanik { namespace Plc {

    /** @name Conversion
     *  @{ */

    /** @brief @p dt_ms as seconds - the unit every gain, rate and time constant
     *  in this library is expressed in. */
    constexpr float seconds(uint32_t dt_ms) {
        return (float)dt_ms / 1000.0f;
    }

    /** @}
     *  @name Stepping a counter
     *  @{ */

    /** @brief Advance @p elapsed by @p dt_ms, saturating at @p limit.
     *
     *  Saturating rather than wrapping matters: a hold far longer than the
     *  limit must stay elapsed, not roll back around to zero.
     *
     *  @note Not constexpr: it writes through a reference, which C++11 does not
     *   allow in a constant expression. Inline for the same reason the rest of
     *   this directory is - it is two instructions in a scan hot path. */
    inline void accumulate(uint32_t& elapsed, uint32_t limit, uint32_t dt_ms) {
        if (elapsed >= limit) return;
        const uint32_t room = limit - elapsed;
        elapsed += (dt_ms < room) ? dt_ms : room;
    }

    /** @brief Count @p dt_ms off @p remaining, stopping at zero. The mirror of
     *  accumulate() for a block holding a time budget rather than a dwell. */
    inline void countdown(uint32_t& remaining, uint32_t dt_ms) {
        remaining = (dt_ms < remaining) ? (remaining - dt_ms) : 0;
    }

    /** @} */

}}  // namespace Botanik::Plc

#endif // PLC_FUNCTIONS_TIME_H
