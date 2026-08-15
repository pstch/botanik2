#ifndef PLC_BLOCKS_LOGIC_H
#define PLC_BLOCKS_LOGIC_H

/**
 * @file logic.h
 * @brief Edges, latches, and the interlock built on them. Untimed: none of
 *        these takes a dt.
 * @note IEC 61131-3: R_TRIG, F_TRIG, SR, RS. Interlock is ours.
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

    /** @}
     *  @name Interlock
     *  @{ */

    /**
     * @brief Four permissives in (true = satisfied), one permit out, with
     *        first-out annunciation. Pass @c true for an unused input.
     * @note Three things make it an interlock rather than an `and`: it latches
     *       and starts WITHOUT the permit, so nothing arms itself after a power
     *       cycle (`Interlock(false)` opts out); the acknowledge is an EDGE, so a
     *       reset wired true cannot silently clear every future trip; and it
     *       remembers which input went first, since the rest usually fall over
     *       as a consequence.
     */
    class Interlock : public Block {

        const char* _names[4];       ///< borrowed, flash-resident labels
        RTrig       _reset_edge;
        bool        _ok[4];          ///< last state of each input
        bool        _permit  = false;
        uint8_t     _first   = NONE; ///< index of the input that tripped first
        bool        _latching;

    public:
        /** @brief first_out() when nothing has tripped. */
        static const uint8_t NONE = 0xFF;

        /** @brief Unnamed inputs; first_out_name() then reports "1".."4". */
        explicit Interlock(bool latching = true);
        /** @brief @p n0..@p n3 are borrowed and must outlive the block - string
         *  literals, in practice. */
        Interlock(const char* n0, const char* n1, const char* n2,
                  const char* n3, bool latching = true);

        /** @brief Evaluate the four permissives.
         *  @param reset acknowledge (rising edge); ignored while any input is
         *         still unsatisfied, so an early press cannot arm the clear.
         *  @return the permit. */
        bool operator()(bool in0, bool in1, bool in2, bool in3,
                        bool reset = false);

        bool permit() const { return _permit; }
        bool tripped() const { return !_permit; }
        /** @brief Index 0..3 of the input that dropped first, or NONE. */
        uint8_t first_out() const { return _first; }
        /** @brief Its label; "" when nothing has tripped. */
        const char* first_out_name() const;
        /** @brief Last state of input @p i (>= 4 reads false). */
        bool input_ok(uint8_t i) const;
        /** @brief Clear the latch and the first-out record by hand. */
        void reset();

    };

    /** @} */

    /** @name Family checks
     *  @{ */
    BOTANIK_PLC_BLOCK(RTrig);
    BOTANIK_PLC_BLOCK(FTrig);
    BOTANIK_PLC_BLOCK(Sr);
    BOTANIK_PLC_BLOCK(Rs);
    BOTANIK_PLC_BLOCK(Interlock);
    /** @} */

}}  // namespace Botanik::Plc

#endif // BOTANIK_FEATURE_PLC_BLK_BASE

#endif // PLC_BLOCKS_LOGIC_H
