/**
 * @file pid.cpp
 * @brief The PID regulator.
 */

#include "libbotanik/plc/blocks/pid.h"

#include "libbotanik/config/base.h"

#include "libbotanik/plc/functions/math.h"
#include "libbotanik/plc/functions/time.h"
#if BOTANIK_FEATURE_PLC_BLK_BASE

namespace Botanik { namespace Plc {

    float Pid::operator()(float sp, float pv, const PidTuning& t,
                          uint32_t dt_ms) {
        const float dt = seconds(dt_ms);

        _error = t.reverse ? (pv - sp) : (sp - pv);

        // First scan: adopt the reading so the derivative starts at zero rather
        // than at (pv - 0) / dt, which is a very large number.
        if (!_seed) { _prev_pv = pv; _seed = true; }

        if (dt > 0.0f) {
            const float raw = (pv - _prev_pv) / dt;
            if (t.d_tau_s > 0.0f) {
                const float a = dt / (t.d_tau_s + dt);
                _deriv += a * (raw - _deriv);
            } else {
                _deriv = raw;
            }
            _prev_pv = pv;
        }

        // Derivative on the measurement: it opposes the reading's movement, so
        // it enters with the sign the action gives it.
        const float p_term = t.kp * _error;
        const float d_term = t.kd * (t.reverse ? _deriv : -_deriv);

        if (t.ki == 0.0f) {
            // No integral action means NO integral term, not a frozen one: ki
            // is operator-editable, and whatever had accumulated would go on
            // commanding the output for ever with nothing able to remove it.
            _integral = 0.0f;
        } else if (dt > 0.0f) {
            // Conditional integration: only when it does not push further into
            // a limit the output is already against.
            const float unsat = p_term + _integral + d_term;
            const bool winding = (unsat >= t.out_max && _error > 0.0f)
                              || (unsat <= t.out_min && _error < 0.0f);
            if (!winding) {
                _integral += t.ki * _error * dt;
                // Clamped so the integral can SPAN the output, NOT to the
                // output's own limits: with a non-zero floor, limit(i, 20, 100)
                // would clamp a fresh integral of 0.01 UP to 20 and the winding
                // test would pin it there for good. Zero has to stay reachable
                // from either side.
                _integral = limit(_integral,
                                  (t.out_min < 0.0f) ? t.out_min : 0.0f,
                                  (t.out_max > 0.0f) ? t.out_max : 0.0f);
            }
        }

        const float raw_out = p_term + _integral + d_term;
        _sat = (raw_out > t.out_max) || (raw_out < t.out_min);
        _out = limit(raw_out, t.out_min, t.out_max);
        return _out;
    }

    void Pid::preset(float out) {
        _integral = out;
        _out      = out;
        _deriv    = 0.0f;
        _sat      = false;
        _seed     = false;   // re-adopt the reading on the next scan
    }

    void Pid::reset() {
        _integral = 0.0f;
        _prev_pv  = 0.0f;
        _deriv    = 0.0f;
        _error    = 0.0f;
        _out      = 0.0f;
        _seed     = false;
        _sat      = false;
    }

}}  // namespace Botanik::Plc

#endif // BOTANIK_FEATURE_PLC_BLK_BASE
