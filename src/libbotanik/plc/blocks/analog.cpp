/**
 * @file analog.cpp
 * @brief Hysteresis / Hysteresis3 / Ramp / Deadband.
 */

#include "libbotanik/plc/blocks/analog.h"

#include "libbotanik/config/base.h"

#include "libbotanik/plc/functions/time.h"
#if BOTANIK_FEATURE_PLC_BLK_BASE

namespace Botanik { namespace Plc {

    bool Hysteresis::operator()(float value, float low, float high) {
        if (value >= high)      _q = true;
        else if (value <= low)  _q = false;
        return _q;
    }

    Hysteresis3::Band Hysteresis3::operator()(float value, float low,
                                              float high, float hyst) {
        if (hyst < 0.0f) hyst = 0.0f;

        // Cold start: place both sub-thresholds from the raw bands. Without it a
        // reading parked between low and low+hyst would read LOW for as long as
        // it sat there, since nothing ever crosses the release edge.
        if (!_seed) {
            _clear_low.preset(value > low);
            _in_high.preset(value >= high);
            _seed = true;
        }

        // Two ordinary two-band thresholds, one per boundary. Each releases
        // hyst away from the edge it caught on.
        const bool clear_low = _clear_low(value, low, low + hyst);
        const bool in_high   = _in_high(value, high - hyst, high);

        // HIGH first: with an over-wide hyst the two can both be asserted, and
        // an over-temperature must not read as a cold call.
        _band = in_high ? BAND_HIGH : (clear_low ? BAND_MID : BAND_LOW);
        return (Band)_band;
    }

    void Hysteresis3::reset() {
        _clear_low.reset();
        _in_high.reset();
        _band = BAND_MID;
        _seed = false;
    }

    uint8_t Staircase3::operator()(float value, float edge1, float edge2,
                                   float edge3, float hyst) {
        if (hyst < 0.0f) hyst = 0.0f;
        // Every rung is evaluated, not just the one above the reading: each
        // holds its own release band, and a rung that stopped being asked
        // would keep whichever side of it the reading was last on.
        uint8_t level = 0;
        if (_rung1(value, edge1 - hyst, edge1)) level = 1;
        if (_rung2(value, edge2 - hyst, edge2)) level = 2;
        if (_rung3(value, edge3 - hyst, edge3)) level = 3;
        _level = level;
        return _level;
    }

    void Staircase3::preset(uint8_t level) {
        _rung1.preset(level >= 1);
        _rung2.preset(level >= 2);
        _rung3.preset(level >= 3);
        _level = level;
    }

    float Ramp::operator()(float target, float rate_per_s, uint32_t dt_ms) {
        if (!_seed) { _out = target; _seed = true; return _out; }
        // A non-positive rate is no motion, not motion the other way: the
        // clamps below are written for a positive step, and a negative one
        // inverts them and walks the output AWAY from its target without bound.
        // The rate is commonly a Tunable off the PARAMS page, so a mistyped sign
        // is operator-reachable. Same rule as scale()'s degenerate span: a
        // misconfigured input gives a dull answer, never a runaway one.
        const float step = (rate_per_s > 0.0f) ? rate_per_s * seconds(dt_ms) : 0.0f;
        float diff = target - _out;
        if (diff >  step) diff =  step;
        if (diff < -step) diff = -step;
        _out += diff;
        return _out;
    }

    float Deadband::operator()(float value, float band) {
        const float diff = value - _held;
        if (!_seed || diff > band || diff < -band) { _held = value; _seed = true; }
        return _held;
    }

}}  // namespace Botanik::Plc

#endif // BOTANIK_FEATURE_PLC_BLK_BASE
