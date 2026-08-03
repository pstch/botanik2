/**
 * @file timers.cpp
 * @brief Ton / Tof / Tp.
 */

#include "timers.h"

#include "common.h"

namespace Botanik { namespace Scan {

    bool Ton::operator()(bool in, uint32_t delay_ms, uint32_t dt_ms) {
        if (!in) { _elapsed = 0; _q = false; return false; }
        accumulate(_elapsed, delay_ms, dt_ms);
        _q = (_elapsed >= delay_ms);
        return _q;
    }

    bool Tof::operator()(bool in, uint32_t delay_ms, uint32_t dt_ms) {
        if (in) { _elapsed = 0; _q = true; return true; }
        if (!_q) return false;
        accumulate(_elapsed, delay_ms, dt_ms);
        if (_elapsed >= delay_ms) _q = false;
        return _q;
    }

    bool Tp::operator()(bool in, uint32_t width_ms, uint32_t dt_ms) {
        if (in && !_prev && !_q) { _q = true; _elapsed = 0; }   // rising edge
        _prev = in;
        if (_q) {
            accumulate(_elapsed, width_ms, dt_ms);
            if (_elapsed >= width_ms) _q = false;
        }
        return _q;
    }

}}  // namespace Botanik::Scan
