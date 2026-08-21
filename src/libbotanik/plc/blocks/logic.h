#ifndef PLC_BLOCKS_LOGIC_H
#define PLC_BLOCKS_LOGIC_H

/**
 * @file logic.h
 * @brief Edges and latches. Untimed: none of these takes a dt.
 * @note IEC 61131-3: R_TRIG, F_TRIG, SR, RS.
 */

#include <cstdint>

#include "libbotanik/config/base.h"
#include "libbotanik/plc/blocks/block.h"
#if BOTANIK_FEATURE_PLC_BLK_BASE

namespace Botanik { namespace Plc {

    /** @name Edges
     *  @{ */

    /** @brief IEC R_TRIG: true for the one scan where the input went false to
     *  true. */
    class RTrig : public Block {
        bool _prev = false;
    public:
        bool operator()(bool in);
        void reset() { _prev = false; }
    };

    /** @brief Falling edge (IEC F_TRIG). */
    class FTrig : public Block {
        // Starts low like RTrig: neither fires on the first scan merely because
        // of where the input happens to sit.
        bool _prev = false;
    public:
        bool operator()(bool in);
        void reset() { _prev = false; }
    };

    /** @}
     *  @name Latches
     *  @{ */

    /** @brief IEC SR: set-dominant latch, set wins when both inputs are true. */
    class Sr : public Block {
        bool _q = false;
    public:
        bool operator()(bool set, bool reset);
        bool q() const { return _q; }
        void reset() { _q = false; }
    };

    /** @brief IEC RS: reset-dominant latch, reset wins when both are true. The
     *  safer default for anything an interlock can stop. */
    class Rs : public Block {
        bool _q = false;
    public:
        bool operator()(bool set, bool reset);
        bool q() const { return _q; }
        void reset() { _q = false; }
    };

    /** @} */

    /** @name Family checks
     *  @{ */
    BOTANIK_PLC_BLOCK(RTrig);
    BOTANIK_PLC_BLOCK(FTrig);
    BOTANIK_PLC_BLOCK(Sr);
    BOTANIK_PLC_BLOCK(Rs);
    /** @} */

}}  // namespace Botanik::Plc

#endif // BOTANIK_FEATURE_PLC_BLK_BASE

#endif // PLC_BLOCKS_LOGIC_H
