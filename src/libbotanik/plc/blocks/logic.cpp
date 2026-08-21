/**
 * @file logic.cpp
 * @brief Edges, latches, interlock.
 */

#include "libbotanik/plc/blocks/logic.h"

#include "libbotanik/config/base.h"
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

    /** @} */

}}  // namespace Botanik::Plc

#endif // BOTANIK_FEATURE_PLC_BLK_BASE
