/**
 * @file filters.cpp
 * @brief Ewma / MedianOf3.
 */

#include "libbotanik/plc/blocks/filters.h"

#include "libbotanik/config/base.h"

#include "libbotanik/plc/functions/time.h"
#if BOTANIK_FEATURE_PLC_BLK_FILTERS

namespace Botanik { namespace Plc {

    float Ewma::operator()(float x, float tau_s, uint32_t dt_ms) {
        if (!_seed) { _value = x; _seed = true; return _value; }
        if (tau_s <= 0.0f) { _value = x; return _value; }

        // dt / (tau + dt) rather than dt / tau: the exact first-order blend, and
        // one that cannot exceed 1 when a scan comes in later than tau.
        const float dt = seconds(dt_ms);
        const float a  = dt / (tau_s + dt);
        _value += a * (x - _value);
        return _value;
    }

    float MedianOf3::operator()(float x) {
        _s[0] = _s[1];
        _s[1] = _s[2];
        _s[2] = x;
        if (_n < 3) ++_n;
        return value();
    }

    float MedianOf3::value() const {
        if (_n == 0) return 0.0f;
        if (_n == 1) return _s[2];
        if (_n == 2) return (_s[1] + _s[2]) * 0.5f;

        // Median of three by pairwise comparison; no sorting, no branch on
        // equality needed.
        const float a = _s[0], b = _s[1], c = _s[2];
        if (a <= b) {
            if (b <= c) return b;
            return (a <= c) ? c : a;
        }
        if (a <= c) return a;
        return (b <= c) ? c : b;
    }

}}  // namespace Botanik::Plc

#endif // BOTANIK_FEATURE_PLC_BLK_FILTERS
