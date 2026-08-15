#ifndef PLC_FUNCTIONS_MATH_H
#define PLC_FUNCTIONS_MATH_H

/**
 * @file math.h
 * @brief Stateless control arithmetic: limit, rescale, select, deadzone, range.
 * @note IEC 61131-3 FUNCTIONs: no state, no dt, same inputs always the same
 *       output. LIMIT and SEL are the standard ones; the rest are ours.
 * @note A function is a single expression in a header; a block needs a .cpp.
 *       That rule is what separates this directory from blocks/.
 */

namespace Botanik { namespace Plc {

    /** @name Limiting
     *  @{ */

    /** @brief @p v held inside [@p lo, @p hi]. IEC LIMIT, in std::clamp
     *  argument order. */
    constexpr float limit(float v, float lo, float hi) {
        return (v < lo) ? lo : ((v > hi) ? hi : v);
    }

    /** @brief Whether @p v is within [@p lo, @p hi], inclusive. */
    constexpr bool in_range(float v, float lo, float hi) {
        return (v >= lo) && (v <= hi);
    }

    /** @brief Zero inside +/- @p band; outside it the EXCURSION beyond the band,
     *  not the raw value, so leaving the band does not step from 0 to @p band. */
    constexpr float deadzone(float v, float band) {
        return (v > band) ? (v - band) : ((v < -band) ? (v + band) : 0.0f);
    }

    /** @}
     *  @name Scaling
     *  @{ */

    /** @brief Rescale @p v from [@p in_lo, @p in_hi] onto [@p out_lo, @p out_hi],
     *  linearly, unclamped. A degenerate input span reads as @p out_lo rather
     *  than dividing by zero: a misconfigured span must not reach an output as
     *  an infinity.
     *  @note For a program's own rescaling. Analog rows carry an AnalogCal. */
    constexpr float scale(float v, float in_lo, float in_hi,
                          float out_lo, float out_hi) {
        return (in_hi == in_lo)
                   ? out_lo
                   : out_lo + (v - in_lo) * (out_hi - out_lo) / (in_hi - in_lo);
    }

    /** @brief @p a at @p t = 0, @p b at @p t = 1. Unclamped: @p t outside
     *  [0, 1] extrapolates. */
    constexpr float lerp(float a, float b, float t) {
        return a + (b - a) * t;
    }

    /** @}
     *  @name Selection
     *  @{ */

    /** @brief IEC SEL: @p in1 when @p g, else @p in0. Reads as data flow
     *  rather than a branch: `sel(night, day_sp, night_sp)`. */
    constexpr float sel(bool g, float in0, float in1) {
        return g ? in1 : in0;
    }

    /** @} */

}}  // namespace Botanik::Plc

#endif // PLC_FUNCTIONS_MATH_H
