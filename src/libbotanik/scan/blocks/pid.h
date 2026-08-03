#ifndef SCAN_BLOCKS_PID_H
#define SCAN_BLOCKS_PID_H

/**
 * @file pid.h
 * @brief The PID regulator and its tuning.
 */

#include <cstdint>

namespace Botanik { namespace Scan {

    /**
     * @brief What a Pid is tuned with: gains, output span, action.
     *
     * Separate from the block because it is DATA - typically built each scan
     * from parameters, so a loop can be retuned from the PARAMS page without a
     * build, while the block keeps the loop's state across the change.
     *
     * Gains are in the parallel (ideal-independent) form, so each is in
     * engineering units and can be reasoned about on its own:
     *
     *   - @c kp : output units per unit of error
     *   - @c ki : output units per (unit of error x second)
     *   - @c kd : output units per (unit of error / second)
     *
     * @note constexpr with defaults, so a site can spell the common cases -
     *  `PidTuning(2.0f)` for pure P, `PidTuning(2.0f, 0.05f)` for PI.
     */
    struct PidTuning {
        float kp;         ///< proportional gain
        float ki;         ///< integral gain, per second
        float kd;         ///< derivative gain, seconds
        float out_min;    ///< output floor
        float out_max;    ///< output ceiling
        float d_tau_s;    ///< derivative lowpass time constant; 0 = unfiltered
        bool  reverse;    ///< reverse-acting: output rises as the reading does

        constexpr PidTuning(float kp_, float ki_ = 0.0f, float kd_ = 0.0f,
                            float out_min_ = 0.0f, float out_max_ = 100.0f,
                            float d_tau_s_ = 0.0f, bool reverse_ = false)
            : kp(kp_), ki(ki_), kd(kd_),
              out_min(out_min_), out_max(out_max_),
              d_tau_s(d_tau_s_), reverse(reverse_) {}
    };

    /**
     * @brief PID regulator: holds a reading at a setpoint by driving a
     * continuous output.
     *
     * Three departures from the textbook sum, each of which is what makes the
     * difference between a loop that works on a greenhouse and one that only
     * works on paper:
     *
     *  - **Derivative on the measurement, not the error.** A setpoint typed at
     *    the console is a step; differentiating it slams the output to a limit
     *    for one scan. Differentiating the reading gives the same damping with
     *    no kick, and is optionally lowpassed (@c d_tau_s) because
     *    differentiating a noisy sensor amplifies exactly the noise.
     *  - **Conditional integration.** While the output sits at a limit the
     *    integral is frozen if it would push further into that limit, so a loop
     *    that spends the night against its ceiling comes off it the moment the
     *    error changes sign, instead of unwinding for an hour first. No
     *    back-calculation gain to tune.
     *  - **The integral is clamped to the output span.** A term larger than any
     *    output the actuator can produce is a value the loop must spend real
     *    time undoing.
     *
     * Direct-acting by default: error is `setpoint - reading`, i.e. the output
     * rises when the reading is too LOW (heating, filling, injecting). A
     * cooling or venting loop sets @c reverse, which flips the error and the
     * derivative together - swapping the two arguments instead would silently
     * differentiate the setpoint.
     *
     * The block holds only state; everything tunable is in PidTuning, passed
     * each call.
     */
    class Pid {

        /** @name State
         *  @{ */

        float _integral = 0.0f;   ///< the I term itself, in output units
        float _prev_pv  = 0.0f;
        float _deriv    = 0.0f;   ///< filtered d(reading)/dt, per second
        float _error    = 0.0f;
        float _out      = 0.0f;
        bool  _seed     = false;
        bool  _sat      = false;

        /** @} */

    public:
        /** @name Operation
         *  @{ */

        /** @brief Run one scan of the loop.
         *  @param sp setpoint, in the reading's units
         *  @param pv the reading (process value)
         *  @return the output, clamped to [out_min, out_max]. */
        float operator()(float sp, float pv, const PidTuning& t, uint32_t dt_ms);

        float value() const { return _out; }
        /** @brief Last error, sign-corrected for the action: positive means the
         *  loop is asking for more output. */
        float error() const { return _error; }
        /** @brief The integral term, in output units - the number to watch when
         *  a loop misbehaves. */
        float integral() const { return _integral; }
        /** @brief Whether the last output hit a limit. */
        bool saturated() const { return _sat; }

        /** @brief Force the output to @p out and make the integral carry it.
         *
         *  For handing a loop back from manual: line the manual output up with
         *  what the loop should produce, preset it, and the first automatic
         *  scan starts from there rather than from zero. The proportional term
         *  still lands on top, so match the setpoint before transferring if the
         *  bump has to be nil. Also re-seeds the derivative, so the jump does
         *  not read as a rate of change. */
        void preset(float out);

        /** @brief Back to a cold loop: no integral, no derivative history. */
        void reset();

        /** @} */
    };

}}  // namespace Botanik::Scan

#endif // SCAN_BLOCKS_PID_H
