/**
 * @file logic.cpp
 * @brief Edges, latches, interlock.
 */

#include "libbotanik/plc/blocks/logic.h"    // RTrig / FTrig / Sr / Rs / Interlock

#include "libbotanik_config.h"              // BOTANIK_FEATURE_PLC_BLK_*
#if BOTANIK_FEATURE_PLC_BLK_BASE

namespace Botanik { namespace Plc {

    /** @name Edges
     *  @{ */

    bool RTrig::operator()(bool in) {
        const bool e = in && !_prev;
        _prev = in;
        return e;
    }

    bool FTrig::operator()(bool in) {
        const bool e = !in && _prev;
        _prev = in;
        return e;
    }

    /** @}
     *  @name Latches
     *  @{ */

    bool Sr::operator()(bool set, bool reset) {
        _q = set || (_q && !reset);
        return _q;
    }

    bool Rs::operator()(bool set, bool reset) {
        _q = !reset && (set || _q);
        return _q;
    }

    /** @}
     *  @name Interlock
     *  @{ */

    // Out-of-line definition for the in-class constant, so taking its address or
    // binding it to a reference links.
    const uint8_t Interlock::NONE;

    Interlock::Interlock(bool latching)
        : Interlock("1", "2", "3", "4", latching) {}

    Interlock::Interlock(const char* n0, const char* n1, const char* n2,
                         const char* n3, bool latching)
        : _latching(latching) {
        _names[0] = n0; _names[1] = n1; _names[2] = n2; _names[3] = n3;
        for (uint8_t i = 0; i < 4; ++i) _ok[i] = true;
    }

    bool Interlock::operator()(bool in0, bool in1, bool in2, bool in3,
                               bool reset) {
        const bool in[4] = { in0, in1, in2, in3 };

        // First-out: record the first input to drop while nothing else had.
        // Held until the reset, so the annunciation outlives the condition.
        bool all_ok = true;
        for (uint8_t i = 0; i < 4; ++i) {
            if (!in[i] && _first == NONE) _first = i;
            if (!in[i]) all_ok = false;
            _ok[i] = in[i];
        }

        // The acknowledge is an edge, and only counts once every permissive is
        // back: a reset held true must not stand as blanket permission for the
        // next trip.
        const bool ack = _reset_edge(reset) && all_ok;

        if (!all_ok)           _permit = false;
        else if (!_latching)   _permit = true;
        else if (ack)          _permit = true;

        if (_permit) _first = NONE;
        return _permit;
    }

    const char* Interlock::first_out_name() const {
        return (_first < 4) ? _names[_first] : "";
    }

    bool Interlock::input_ok(uint8_t i) const {
        return (i < 4) && _ok[i];
    }

    void Interlock::reset() {
        _permit = false;
        _first  = NONE;
        _reset_edge.reset();
        for (uint8_t i = 0; i < 4; ++i) _ok[i] = true;
    }

    /** @} */

}}  // namespace Botanik::Plc

#endif // BOTANIK_FEATURE_PLC_BLK_BASE
