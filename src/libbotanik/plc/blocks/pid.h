#ifndef PLC_BLOCKS_PID_H
#define PLC_BLOCKS_PID_H

/**
 * @file pid.h
 * @brief The PID regulator and its tuning.
 */

#include <cstdint>              // uint32_t

#include "libbotanik/config/base.h"       // the BOTANIK_FEATURE_* gates
#include "libbotanik/plc/blocks/block.h"  // Plc::Block / BOTANIK_PLC_BLOCK
#if BOTANIK_FEATURE_PLC_BLK_BASE

namespace Botanik { namespace Plc {

    /**
     * @brief What a Pid is tuned with: gains, output span, action.
     *
     * Data, not state: typically rebuilt each scan from parameters, so a loop is
     * retunable from the PARAMS page while the block keeps its history. Gains
     * are the parallel form - @c kp is output units per unit of error, @c ki per
     * (error x second), @c kd per (error / second).
     *
     * @note constexpr with defaults: `PidTuning(2.0f)` is pure P,
     *  `PidTuning(2.0f, 0.05f)` is PI.
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
     * Three departures from the textbook sum:
     *
     *  - **Derivative on the measurement, not the error**, so a setpoint typed
     *    at the console does not slam the output to a limit for one scan;
     *    optionally lowpassed (@c d_tau_s), since differentiating a noisy sensor
     *    amplifies exactly the noise.
     *  - **Conditional integration**: frozen while it would push further into a
     *    limit the output is already against, so a loop that spent the night on
     *    its ceiling comes off it when the error changes sign. No
     *    back-calculation gain to tune.
     *  - **The integral is clamped to the output span** - a term larger than any
     *    output the actuator can produce is time the loop must spend undoing it.
     *
     * Direct-acting by default (output rises as the reading falls: heating,
     * filling). @c reverse flips the error and the derivative together; swapping
     * the two arguments instead would silently differentiate the setpoint.
     */
    class Pid : public Block {

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
         *  not read as a rate of change.
         *
         *  @note Meaningless with @c ki at 0: a loop with no integral action
         *  carries no integral, so the next scan clears it. Preset a PI loop, or
         *  add the bias where it belongs. */
        void preset(float out);

        /** @brief Back to a cold loop: no integral, no derivative history. */
        void reset();

        /** @} */
    };

    /** @name Family checks
     *  @see block.h - every block is reset on a control withdrawal.
     *  @{ */
    BOTANIK_PLC_BLOCK(Pid);
    /** @} */

}}  // namespace Botanik::Plc

#endif // BOTANIK_FEATURE_PLC_BLK_BASE

#endif // PLC_BLOCKS_PID_H
