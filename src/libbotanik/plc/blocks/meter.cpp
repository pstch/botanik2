/**
 * @file meter.cpp
 * @brief Integral / Derivative.
 */

#include "libbotanik/plc/blocks/meter.h"    // Integral / Derivative

#include "libbotanik/config/base.h"       // the BOTANIK_FEATURE_* gates

#include "libbotanik/plc/functions/time.h"  // Plc::seconds
#if BOTANIK_FEATURE_PLC_BLK_BASE

namespace Botanik { namespace Plc {

    float Integral::operator()(float rate_per_s, uint32_t dt_ms, float cutoff) {
        float rate = rate_per_s;
        if (cutoff > 0.0f && rate < cutoff && rate > -cutoff) rate = 0.0f;
        _rate = rate;
        _total += (double)rate * (double)seconds(dt_ms);
        return (float)_total;
    }

    float Derivative::operator()(float quantity, uint32_t window_ms,
                                   uint32_t dt_ms) {
        if (!_seed) { _prev = quantity; _seed = true; _elapsed = 0; return 0.0f; }

        _elapsed += dt_ms;
        if (_elapsed == 0 || _elapsed < window_ms) return _rate;

        // Over the time actually accumulated, not the nominal window: a late
        // scan overshoots it, and the nominal figure would report a rate that
        // never happened.
        _rate = (quantity - _prev) / seconds(_elapsed);
        _prev = quantity;
        _elapsed = 0;
        return _rate;
    }

    void Derivative::preset(float quantity) {
        _prev = quantity;
        _elapsed = 0;
        _seed = true;
    }

    void Derivative::reset() {
        _prev = 0.0f;
        _rate = 0.0f;
        _elapsed = 0;
        _seed = false;
    }

}}  // namespace Botanik::Plc

#endif // BOTANIK_FEATURE_PLC_BLK_BASE
