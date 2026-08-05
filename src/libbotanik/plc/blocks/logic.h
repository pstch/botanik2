#ifndef PLC_BLOCKS_LOGIC_H
#define PLC_BLOCKS_LOGIC_H

/**
 * @file logic.h
 * @brief Edges, latches, and the interlock built on them.
 *
 * Untimed: none of these takes a dt, because none of them measures time.
 */

#include <cstdint>

#include "../../../libbotanik_config.h"
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
     * @brief Four-input interlock with first-out annunciation.
     *
     * Every input is a PERMISSIVE: true means "this condition is satisfied".
     * The permit is true only while all four are, and - by default - stays down
     * once dropped until a reset is acknowledged, even if the cause cleared
     * itself. That latch is the point of an interlock: a condition that trips
     * and recovers on its own must still be seen and cleared by hand.
     *
     * FIRST-OUT is the other half. When four things can stop a machine, the one
     * that matters is which went first; the rest usually fall over as a
     * consequence. first_out() names it, and holds it until the reset, so a log
     * line or an alarm note can say WHY rather than just "interlocked".
     *
     * The reset is edge-triggered, so a reset input wired true (a stuck button,
     * a forgotten override) cannot silently auto-clear every future trip.
     *
     * A latching interlock starts WITHOUT the permit and needs one acknowledge
     * even if it never tripped: after a power cycle nothing should arm itself,
     * for the same reason control authority starts disabled. A non-latching one
     * simply follows the four inputs.
     *
     * Four inputs, no more: a site with three passes @c true for the fourth,
     * and one with six chains two interlocks. Fixed width keeps first_out()
     * meaningful and the block free of any storage to size.
     */
    class Interlock {

        /** @name State
         *  @{ */

        const char* _names[4];       ///< borrowed, flash-resident labels
        RTrig       _reset_edge;
        bool        _ok[4];          ///< last state of each input
        bool        _permit  = false;
        uint8_t     _first   = NONE; ///< index of the input that tripped first
        bool        _latching;

        /** @} */

    public:
        /** @brief first_out() when nothing has tripped. */
        static const uint8_t NONE = 0xFF;

        /** @name Lifecycle
         *  @{ */

        /** @brief Unnamed inputs; first_out_name() then reports "1".."4". */
        explicit Interlock(bool latching = true);
        /** @brief Named inputs, for the log line. The names are borrowed and
         *  must outlive the block - string literals, in practice. */
        Interlock(const char* n0, const char* n1, const char* n2,
                  const char* n3, bool latching = true);

        /** @} */
        /** @name Operation
         *  @{ */

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

        /** @} */
    };

    /** @} */

}}  // namespace Botanik::Plc

#endif // BOTANIK_FEATURE_PLC_BLK_BASE

#endif // PLC_BLOCKS_LOGIC_H
