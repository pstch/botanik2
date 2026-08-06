#ifndef PLC_FUNCTIONS_MATH_H
#define PLC_FUNCTIONS_MATH_H

/**
 * @file math.h
 * @brief The arithmetic a control program keeps rewriting: limit, rescale,
 *        select, deadzone, range test.
 *
 * IEC 61131-3 FUNCTIONs: no state, no dt, same inputs always the same output.
 * They are `constexpr` and header-only on purpose - see @ref plc_functions for
 * why that rule is what separates this directory from the blocks one.
 */

namespace Botanik { namespace Plc {

    /** @name Limiting
     *  @{ */

    /** @brief @p v held inside [@p lo, @p hi]. IEC's LIMIT, in std::clamp
     *  argument order - every call site here reads "this value, between these
     *  bounds". */
    constexpr float limit(float v, float lo, float hi) {
        return (v < lo) ? lo : ((v > hi) ? hi : v);
    }

    /** @brief Whether @p v is within [@p lo, @p hi], inclusive. The
     *  plausibility test said once - `!in_range(ln, 200.0f, 260.0f)` rather
     *  than a pair of comparisons whose bounds must be read twice to trust. */
    constexpr bool in_range(float v, float lo, float hi) {
        return (v >= lo) && (v <= hi);
    }

    /** @brief Zero inside +/- @p band, and outside it the EXCURSION beyond the
     *  band rather than the raw value - continuous, because a step from 0 to
     *  `band` on leaving would kick whatever acts on it.
     *
     *  The stateless cousin of the Deadband block, which suppresses reports of
     *  small moves rather than ignoring a small error. */
    constexpr float deadzone(float v, float band) {
        return (v > band) ? (v - band) : ((v < -band) ? (v + band) : 0.0f);
    }

    /** @}
     *  @name Scaling
     *  @{ */

    /** @brief Rescale @p v from [@p in_lo, @p in_hi] onto [@p out_lo,
     *  @p out_hi], linearly and without clamping. A degenerate input span reads
     *  as @p out_lo rather than dividing by zero - a misconfigured span must
     *  give a dull answer, not an infinity that reaches an output.
     *
     *  @note Not for analog rows, which carry an AnalogCal whose six Tunables
     *   can each be a parameter. This is for a program's own rescaling, e.g. a
     *   0..1 demand onto a 20..80% stroke. */
    constexpr float scale(float v, float in_lo, float in_hi,
                          float out_lo, float out_hi) {
        return (in_hi == in_lo)
                   ? out_lo
                   : out_lo + (v - in_lo) * (out_hi - out_lo) / (in_hi - in_lo);
    }

    /** @brief Linear interpolation: @p a at @p t = 0, @p b at @p t = 1.
     *  Not clamped - @p t outside [0, 1] extrapolates. */
    constexpr float lerp(float a, float b, float t) {
        return a + (b - a) * t;
    }

    /** @}
     *  @name Selection
     *  @{ */

    /** @brief IEC's SEL: @p in1 when @p g, else @p in0. Worth a name because it
     *  reads as data flow rather than a branch - `sel(night, day_sp, night_sp)`. */
    constexpr float sel(bool g, float in0, float in1) {
        return g ? in1 : in0;
    }

    /** @} */

}}  // namespace Botanik::Plc

#endif // PLC_FUNCTIONS_MATH_H
