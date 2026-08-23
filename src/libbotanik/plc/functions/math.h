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

#include <cmath>
#include <cstdint>

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

    /** @brief The middle of three, discarding both extremes.
     *
     *  SPATIAL, not temporal: three probes reading one place at one instant,
     *  where Plc::MedianOf3 is three successive samples of one probe. A probe
     *  stuck high or low can never be the answer. */
    constexpr float median3(float a, float b, float c) {
        return (a < b) ? ((b < c) ? b : ((a < c) ? c : a))
                       : ((a < c) ? a : ((b < c) ? c : b));
    }

    /** @brief The mean of the two CLOSEST of three: the outlier is dropped and
     *  the agreeing pair averaged.
     *
     *  What a three-probe zone reduces to. Against a plain median it keeps the
     *  information in the second good probe instead of throwing it away, so the
     *  result is quieter by root-two while rejecting one bad probe just as
     *  completely - the outlier is by construction the one furthest from the
     *  other two. */
    inline float closest2_mean(float a, float b, float c) {
        return (fabsf(a - b) <= fabsf(b - c))
             ? ((fabsf(a - b) <= fabsf(a - c)) ? 0.5f * (a + b) : 0.5f * (a + c))
             : ((fabsf(b - c) <= fabsf(a - c)) ? 0.5f * (b + c) : 0.5f * (a + c));
    }

    /** @brief IEC MAX / MIN, two arguments. Written out because a fusion of
     *  three demands is the shape half these programs have, and the
     *  if-it-is-bigger-keep-it spelling hides it. */
    constexpr float max_of(float a, float b) { return (a > b) ? a : b; }
    constexpr float min_of(float a, float b) { return (a < b) ? a : b; }

    /** @brief IEC MAX, four arguments. A fusion of four demands is one node
     *  rather than a tree of three, which is how the law reads. */
    constexpr float max4(float a, float b, float c, float d) {
        return max_of(max_of(a, b), max_of(c, d));
    }

    /** @brief IEC SUB / ABS. Named for the same reason MAX and MIN are: a law
     *  written in the vocabulary reads as the network it is, and a gap between
     *  two levels is one SUB into one ABS rather than an expression. */
    constexpr float sub(float a, float b) { return a - b; }
    constexpr float abs_of(float v) { return (v < 0.0f) ? -v : v; }

    /** @brief IEC MUX, four inputs: @p k picks one of @p in0 .. @p in3, and
     *  anything out of range reads as @p in0. What an array indexed by a
     *  staircase level is, with the selection named rather than implied. */
    constexpr float mux4(uint8_t k, float in0, float in1, float in2, float in3) {
        return (k == 1) ? in1 : ((k == 2) ? in2 : ((k == 3) ? in3 : in0));
    }

    /** @}
     *  @name Control ladders
     *
     *  Every climate ladder has one shape: a quantity brought to a demand the
     *  actuators spread themselves along. Written once, so three subsystems
     *  cannot disagree about what "half demand" means.
     *  @{ */

    /** @brief @p v mapped onto 0..1 across [@p lo, @p hi] and clamped.
     *  A degenerate or inverted span reads as fully on at or above @p hi, so a
     *  mis-set pair fails toward acting rather than toward doing nothing. */
    constexpr float ladder(float v, float lo, float hi) {
        return (hi <= lo) ? ((v >= hi) ? 1.0f : 0.0f)
                          : limit((v - lo) / (hi - lo), 0.0f, 1.0f);
    }

    /** @brief The same either side of zero, with a dead band between.
     *
     *  Reads 0 inside [@p neg_start, @p pos_start], falls to -1 by @p neg_full
     *  and rises to +1 by @p pos_full. What the cooling and humidity ladders
     *  produce: one signed number, negative meaning the opposite process.
     *  @param neg_full at or below this the negative branch is fully on
     *  @param pos_full at or above this the positive branch is fully on */
    constexpr float signed_ladder(float v, float neg_full, float neg_start,
                                  float pos_start, float pos_full) {
        return (v >= pos_start) ? ladder(v, pos_start, pos_full)
             : (v <= neg_start) ? -ladder(-v, -neg_start, -neg_full)
                                : 0.0f;
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
