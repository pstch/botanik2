#ifndef PLC_BLOCKS_LOGIC_H
#define PLC_BLOCKS_LOGIC_H

/**
 * @file logic.h
 * @brief Edges, latches, and the interlock built on them.
 *
 * Untimed: none of these takes a dt, because none of them measures time.
 */

#include <cstdint>                    // uint8_t - the Interlock permissive index and first-out slot

#include "libbotanik_config.h"        // BOTANIK_FEATURE_PLC_BLK_BASE - the gate this whole header sits behind
#if BOTANIK_FEATURE_PLC_BLK_BASE

namespace Botanik { namespace Plc {

    /** @name Edges
     *  @{ */

    /** @brief Rising edge: true for the one scan where the input went false to
     *  true. */
    class RTrig {
        bool _prev = false;
    public:
        bool operator()(bool in);
        void reset() { _prev = false; }
    };

    /** @brief Falling edge. */
    class FTrig {
        // Assumes the input starts low, matching RTrig: neither fires on the
        // first scan merely because of where the input happens to sit.
        bool _prev = false;
    public:
        bool operator()(bool in);
        void reset() { _prev = false; }
    };

    /** @}
     *  @name Latches
     *  @{ */

    /** @brief Set-dominant latch: set wins when both inputs are true. */
    class Sr {
        bool _q = false;
    public:
        bool operator()(bool set, bool reset);
        bool q() const { return _q; }
        void reset() { _q = false; }
    };

    /** @brief Reset-dominant latch: reset wins when both inputs are true. The
     *  safer default for anything an interlock can stop. */
    class Rs {
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
     * @brief Four permissives in, one permit out, with first-out annunciation.
     *
     * Every input is a PERMISSIVE - true means satisfied - and the permit holds
     * only while all four are. Three rules make it an interlock rather than an
     * `and`:
     *
     *  - **It latches.** A condition that trips and recovers on its own must
     *    still be seen and cleared by hand. A latching interlock also starts
     *    WITHOUT the permit, so nothing arms itself after a power cycle - the
     *    same reasoning as control authority starting disabled. `Interlock(false)`
     *    opts out and just follows the inputs.
     *  - **The acknowledge is an edge**, so a reset wired true (a stuck button,
     *    a forgotten override) cannot silently clear every future trip.
     *  - **It remembers which input went first.** The rest usually fall over as
     *    a consequence, so first_out() is what lets a log line say why rather
     *    than just "interlocked". Held until the acknowledge.
     *
     * Four inputs, no more: a site with three passes @c true for the fourth, one
     * with six chains two interlocks. Fixed width keeps first_out() meaningful
     * and the block free of storage to size.
     */
    class Interlock {

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
        /** @brief Named inputs, for the log line. The names are borrowed and
         *  must outlive the block - string literals, in practice. */
        Interlock(const char* n0, const char* n1, const char* n2,
                  const char* n3, bool latching = true);

        /** @brief Evaluate the four permissives.
         *  @param reset acknowledge (rising edge); ignored while any input is
         *         still unsatisfied, so an early press cannot arm the clear.
         *  @return the permit. */
        bool operator()(bool in0, bool in1, bool in2, bool in3,
                        bool reset = false);

        bool permit() const { return _permit; }
        /** @brief The complement of permit(), for readability at the call site. */
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

}}  // namespace Botanik::Plc

#endif // BOTANIK_FEATURE_PLC_BLK_BASE

#endif // PLC_BLOCKS_LOGIC_H
