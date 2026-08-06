/**
 * @file actuator.cpp
 * @brief The two-position actuator state machine.
 */

#include "libbotanik/plc/blocks/actuator.h" // Actuator - the five-state stroke-timing machine implemented here

#include "libbotanik_config.h"        // BOTANIK_FEATURE_PLC_BLK_* - the gate the bodies below sit behind

#include "libbotanik/plc/functions/math.h" // Plc::limit - clamp the position estimate to [0,1]
#include "libbotanik/plc/functions/time.h" // Plc::countdown - times the open/close strokes
#if BOTANIK_FEATURE_PLC_BLK_BASE

namespace Botanik { namespace Plc {

    void Actuator::step(float target, const ActuatorConfig& cfg,
                        uint32_t dt_ms) {
        target = limit(target, 0.0f, 1.0f);
        _reverse_ms = cfg.reverse_ms;

        if (_run_ms > 0) {
            // Only the part of this scan the coil was actually energised for
            // counts as travel; the rest of it belongs to the next decision.
            const uint32_t ran = (dt_ms < _run_ms) ? dt_ms : _run_ms;
            travel(ran, cfg);
            _run_ms -= ran;

            if (_run_ms == 0) {
                arrive();
            } else if (!_datum) {
                // The target moved the other way mid-stroke. A stroke is
                // otherwise COMMITTED - a seating run must be allowed to
                // overrun the estimate, which is the whole point of it - so
                // only the opposite direction interrupts, never "we are close
                // enough now".
                const Dir w = want(target, cfg);
                if (w != D_NONE && w != (Dir)_dir) {
                    _run_ms  = 0;
                    _seating = false;
                    _dir     = D_NONE;
                    _wait_ms = _reverse_ms;
                    settle();
                }
            }
        } else if (_wait_ms > 0) {
            countdown(_wait_ms, dt_ms);
        }

        if (_run_ms == 0 && _wait_ms == 0) start(target, cfg);

        _open  = (_dir == D_OPEN)  && (_run_ms > 0);
        _close = (_dir == D_CLOSE) && (_run_ms > 0);
    }

    Actuator::Dir Actuator::want(float target, const ActuatorConfig& cfg) const {
        // Nothing is known about where it sits, so there is no error to compute:
        // drive to whichever end the target is nearer to being, and datum there.
        if (_state == ST_UNKNOWN) {
            return (target >= 1.0f - cfg.deadband) ? D_OPEN : D_CLOSE;
        }
        const float err = target - _pos;
        if (err >  cfg.deadband) return D_OPEN;
        if (err < -cfg.deadband) return D_CLOSE;
        return D_NONE;
    }

    void Actuator::travel(uint32_t ran, const ActuatorConfig& cfg) {
        const uint32_t span = (_dir == D_OPEN) ? cfg.open_ms : cfg.close_ms;
        // A zero travel time is a misconfiguration, not a divide: treat the
        // actuator as instant rather than producing an infinity.
        const float d = (span > 0) ? ((float)ran / (float)span) : 1.0f;
        _pos = limit(_pos + ((_dir == D_OPEN) ? d : -d), 0.0f, 1.0f);
    }

    void Actuator::start(float target, const ActuatorConfig& cfg) {
        const Dir w = want(target, cfg);
        if (w == D_NONE) return;

        const bool datum  = (_state == ST_UNKNOWN);
        const bool to_end = datum || (target <= cfg.deadband)
                                  || (target >= 1.0f - cfg.deadband);
        const uint32_t span = (w == D_OPEN) ? cfg.open_ms : cfg.close_ms;

        // A datum stroke assumes the worst case, a full travel away.
        float distance = datum ? 1.0f : (target - _pos);
        if (distance < 0.0f) distance = -distance;

        uint32_t run = (uint32_t)(distance * (float)span);
        if (to_end) {
            run += cfg.seat_ms;         // overrun onto the stop, re-datuming
        } else if (run < cfg.min_run_ms) {
            return;                     // not worth a twitch
        }
        if (run == 0) run = 1;          // always make progress

        _dir     = w;
        _run_ms  = run;
        _seating = to_end;
        _datum   = datum;
        _state   = (w == D_OPEN) ? ST_OPENING : ST_CLOSING;
    }

    void Actuator::arrive() {
        // The stroke ran past the mechanical stop, so the estimate is now a
        // fact rather than an integration.
        if (_seating) _pos = (_dir == D_OPEN) ? 1.0f : 0.0f;
        _dir     = D_NONE;
        _seating = false;
        _datum   = false;
        // Dead time after every stroke, not only after an interrupted one: the
        // move that follows one which just seated can only be the opposite
        // direction, and it would otherwise start in this very scan.
        _wait_ms = _reverse_ms;
        settle();
    }

    void Actuator::settle() {
        // CLOSED is a claim about the seat, and only a seating stroke puts the
        // estimate exactly on it. Anything else at rest is open, however
        // slightly.
        _state = (_pos <= 0.0f) ? ST_CLOSED : ST_OPEN;
    }

    void Actuator::stop() {
        // Whatever restarts after this waits out the dead time first: stop()
        // is most often called to hand over between two demands, and the next
        // one may well be the other direction.
        if (_run_ms > 0) _wait_ms = _reverse_ms;
        _run_ms  = 0;
        _dir     = D_NONE;
        _open    = false;
        _close   = false;
        // A stroke cut short never reached its seat, and a datum stroke cut
        // short establishes nothing.
        if (_datum) { _state = ST_UNKNOWN; }
        else        { settle(); }
        _seating = false;
        _datum   = false;
    }

    const char* Actuator::state_name() const {
        switch (_state) {
            case ST_CLOSING: return "CLOSING";
            case ST_CLOSED:  return "CLOSED";
            case ST_OPENING: return "OPENING";
            case ST_OPEN:    return "OPEN";
            default:         return "UNKNOWN";
        }
    }

    void Actuator::home() {
        stop();
        _pos   = 0.0f;
        _state = ST_UNKNOWN;
    }

    void Actuator::preset(float pos) {
        stop();
        _pos = limit(pos, 0.0f, 1.0f);
        settle();
    }

}}  // namespace Botanik::Plc

#endif // BOTANIK_FEATURE_PLC_BLK_BASE
