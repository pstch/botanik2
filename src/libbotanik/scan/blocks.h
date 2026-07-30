#ifndef SCAN_BLOCKS_H
#define SCAN_BLOCKS_H

/**
 * @file blocks.h
 * @brief Function blocks: the small stateful primitives control programs are
 *        built from.
 *
 * These are the PLC idioms - on-delay, edge, latch, hysteresis - as ordinary
 * C++ objects. A program declares them as statics or members and calls them
 * each scan; they hold their own state and nothing else needs to know about it.
 *
 * Every timed block takes @p dt_ms (ScanContext::dt_ms), the ACTUAL elapsed
 * time since that task last ran, rather than reading a clock itself. Two
 * reasons: a task that ran late must not lose the time it was late by, and a
 * block driven purely by its inputs is trivially testable without a clock.
 *
 * All state is RAM; unlike the descriptor tables there is no flash-residency
 * constraint here, so these are ordinary classes.
 */

#include <cstdint>

namespace Botanik { namespace Scan {

    /** @name Timers
     *  @{ */

    /** @brief On-delay. Output goes true once the input has been continuously
     *  true for @p delay_ms, and false immediately when the input drops. The
     *  workhorse: debounce, dwell, "has been wet for 30 s". */
    class Ton {
        uint32_t _elapsed = 0;
        bool     _q       = false;
    public:
        bool operator()(bool in, uint32_t delay_ms, uint32_t dt_ms) {
            if (!in) { _elapsed = 0; _q = false; return false; }
            // Saturate rather than wrap: a very long hold must stay elapsed.
            if (_elapsed < delay_ms) {
                uint32_t room = delay_ms - _elapsed;
                _elapsed += (dt_ms < room) ? dt_ms : room;
            }
            _q = (_elapsed >= delay_ms);
            return _q;
        }
        bool q() const { return _q; }
        void reset() { _elapsed = 0; _q = false; }
    };

    /** @brief Off-delay. Output follows the input up, and holds true for
     *  @p delay_ms after the input drops. Minimum run time, fan overrun. */
    class Tof {
        uint32_t _elapsed = 0;
        bool     _q       = false;
    public:
        bool operator()(bool in, uint32_t delay_ms, uint32_t dt_ms) {
            if (in) { _elapsed = 0; _q = true; return true; }
            if (!_q) return false;
            if (_elapsed < delay_ms) {
                uint32_t room = delay_ms - _elapsed;
                _elapsed += (dt_ms < room) ? dt_ms : room;
            }
            if (_elapsed >= delay_ms) _q = false;
            return _q;
        }
        bool q() const { return _q; }
        void reset() { _elapsed = 0; _q = false; }
    };

    /** @brief Pulse. A rising input starts a fixed @p width_ms pulse; the pulse
     *  runs to completion whatever the input does afterwards. */
    class Tp {
        uint32_t _elapsed = 0;
        bool     _q       = false;
        bool     _prev    = false;
    public:
        bool operator()(bool in, uint32_t width_ms, uint32_t dt_ms) {
            if (in && !_prev && !_q) { _q = true; _elapsed = 0; }   // rising edge
            _prev = in;
            if (_q) {
                if (_elapsed < width_ms) {
                    uint32_t room = width_ms - _elapsed;
                    _elapsed += (dt_ms < room) ? dt_ms : room;
                }
                if (_elapsed >= width_ms) _q = false;
            }
            return _q;
        }
        bool q() const { return _q; }
        void reset() { _elapsed = 0; _q = false; _prev = false; }
    };

    /** @}
     *  @name Edges and latches
     *  @{ */

    /** @brief Rising edge: true for the one scan where the input went false to
     *  true. */
    class RTrig {
        bool _prev = false;
    public:
        bool operator()(bool in) { bool e = in && !_prev; _prev = in; return e; }
        void reset() { _prev = false; }
    };

    /** @brief Falling edge. */
    class FTrig {
        // Assumes the input starts low, matching RTrig: neither fires on the
        // first scan merely because of where the input happens to sit.
        bool _prev = false;
    public:
        bool operator()(bool in) { bool e = !in && _prev; _prev = in; return e; }
        void reset() { _prev = false; }
    };

    /** @brief Set-dominant latch: set wins when both inputs are true. */
    class Sr {
        bool _q = false;
    public:
        bool operator()(bool set, bool reset) {
            _q = set || (_q && !reset);
            return _q;
        }
        bool q() const { return _q; }
        void reset() { _q = false; }
    };

    /** @brief Reset-dominant latch: reset wins when both inputs are true. The
     *  safer default for anything an interlock can stop. */
    class Rs {
        bool _q = false;
    public:
        bool operator()(bool set, bool reset) {
            _q = !reset && (set || _q);
            return _q;
        }
        bool q() const { return _q; }
        void reset() { _q = false; }
    };

    /** @}
     *  @name Analog
     *  @{ */

    /** @brief Threshold with hysteresis. Goes true at or above @p high, false
     *  at or below @p low, and holds its state between them. What stops an
     *  actuator chattering when a reading sits on the setpoint. */
    class Hysteresis {
        bool _q = false;
    public:
        bool operator()(float value, float low, float high) {
            if (value >= high)      _q = true;
            else if (value <= low)  _q = false;
            return _q;
        }
        bool q() const { return _q; }
        void reset() { _q = false; }
    };

    /** @brief Rate limiter. Moves the output toward @p target by at most
     *  @p rate_per_s, so a step demand becomes a ramp the mechanics can
     *  follow. */
    class Ramp {
        float _out  = 0.0f;
        bool  _seed = false;
    public:
        float operator()(float target, float rate_per_s, uint32_t dt_ms) {
            if (!_seed) { _out = target; _seed = true; return _out; }
            float step = rate_per_s * (float)dt_ms / 1000.0f;
            float diff = target - _out;
            if (diff >  step) diff =  step;
            if (diff < -step) diff = -step;
            _out += diff;
            return _out;
        }
        float value() const { return _out; }
        /** @brief Jump to @p v without ramping (mode changes, first enable). */
        void preset(float v) { _out = v; _seed = true; }
    };

    /** @brief Deadband: reports @p value only once it has moved more than
     *  @p band from the last reported figure. Suppresses jitter on a noisy
     *  reading without lagging a real change. */
    class Deadband {
        float _held = 0.0f;
        bool  _seed = false;
    public:
        float operator()(float value, float band) {
            float diff = value - _held;
            if (!_seed || diff > band || diff < -band) { _held = value; _seed = true; }
            return _held;
        }
        float value() const { return _held; }
    };

    /** @} */

}}  // namespace Botanik::Scan

#endif // SCAN_BLOCKS_H
