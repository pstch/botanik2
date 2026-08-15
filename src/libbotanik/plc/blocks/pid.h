#ifndef PLC_BLOCKS_PID_H
#define PLC_BLOCKS_PID_H

/**
 * @file pid.h
 * @brief The PID regulator and its tuning.
 * @note IEC 61131-3: PID.
 */

#include <cstdint>

#include "libbotanik/config/base.h"
#include "libbotanik/plc/blocks/block.h"
#if BOTANIK_FEATURE_PLC_BLK_BASE

namespace Botanik { namespace Plc {

    /**
     * @brief What a Pid is tuned with. Data, not state: typically rebuilt each
     *        scan from parameters, so a loop retunes from the PARAMS page while
     *        the block keeps its history.
     * @note Parallel form. constexpr with defaults: `PidTuning(2.0f)` is pure P,
     *       `PidTuning(2.0f, 0.05f)` is PI.
     */
    struct PidTuning {
        float kp;         ///< proportional gain, output units per unit of error
        float ki;         ///< integral gain, per (error x second)
        float kd;         ///< derivative gain, per (error / second)
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
     * @brief PID regulator: holds a reading at a setpoint. Direct-acting by
     *        default (output rises as the reading falls: heating, filling).
     * @note Three departures from the textbook sum, all affecting tuning:
     *       derivative on the measurement rather than the error (a typed setpoint
     *       does not slam the output); conditional integration, so no
     *       back-calculation gain to tune; integral clamped to the output span.
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
        /** @brief Sign-corrected for the action: positive means the loop is
         *  asking for more output. */
        float error() const { return _error; }
        /** @brief In output units - the number to watch when a loop misbehaves. */
        float integral() const { return _integral; }
        bool saturated() const { return _sat; }

        /** @brief Force the output to @p out and make the integral carry it, for
         *  handing a loop back from manual. Also re-seeds the derivative.
         *  @note Meaningless with @c ki at 0: a loop with no integral action
         *  carries none, so the next scan clears it. The proportional term still
         *  lands on top, so match the setpoint first for a bumpless transfer. */
        void preset(float out);

        /** @brief Back to a cold loop: no integral, no derivative history. */
        void reset();

        /** @} */
    };

    /** @name Family checks
     *  @{ */
    BOTANIK_PLC_BLOCK(Pid);
    /** @} */

}}  // namespace Botanik::Plc

#endif // BOTANIK_FEATURE_PLC_BLK_BASE

#endif // PLC_BLOCKS_PID_H
