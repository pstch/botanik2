// REVIEW_35: TODO
#ifndef CONSOLE_ALARM_BAR_H
#define CONSOLE_ALARM_BAR_H

/**
 * @file alarm_bar.h
 * @brief AlarmBar - the terminal-owned bottom-row alarm strip.
 *
 * A reverse-video bar on the physical last row (reserved by the terminal when
 * an alarm model is wired), showing " ALARMS : " then the unmasked, un-ack'd
 * alarm codes drawn normal-video so they read as a set. Owned by AnsiTerminal
 * and submitted as a transaction from step() only when the unmasked set changes
 * or the row was wiped (mark_dirty()); the TX queue paces + coalesces it. No
 * change-highlighting - the set is visible as a whole.
 */

#include <cstdint>

namespace Botanik { namespace Alarms { class AlarmModel; } }

namespace Botanik { namespace Terminal {

    class AnsiTerminal;
    class Console;

    /** @brief Bottom-row alarm strip (see file header). */
    class AlarmBar {

        /** @name State
         *  @{ */

        /** @brief The console this bar belongs to (alarm source). */
        Console&            _c;
        /** @brief The VT device it paints through (cached _c.terminal()). */
        AnsiTerminal&       _t;
        /** @brief Unmasked alarm set last painted (bit i = table row i). */
        uint64_t            _sig = 0;
        /** @brief A repaint is due (set on change / screen wipe). */
        bool                _dirty = true;

        /** @} */

    public:
        /** @brief Bind the bar to its owning console (caches its device). */
        explicit AlarmBar(Console& console);

        /** @name Cooperative paint
         *  @{ */

        /** @brief Per-step pass: submit the bar as a transaction when the
         *  unmasked set changed or the row was wiped. The queue paces it, and the
         *  drain's cursor re-assertion keeps a visible page cursor (the shell
         *  editor's) in place. No-op without an alarm source. */
        void step();

        /** @brief Flag a repaint (after a full-screen clear or page switch wiped
         *  the reserved row). */
        void mark_dirty() { _dirty = true; }

        /** @} */

    private:
        /** @name Internals
         *  @{ */

        /** @brief Bitset of currently-unmasked, un-ack'd alarms (the bar's
         *  listed set), used as the change signature gating a repaint. Zero when
         *  there is no alarm source. */
        uint64_t unmasked_set() const;

        /** @brief Repaint the whole bar unconditionally (the gate and cursor
         *  save/restore live in step()). */
        void draw();

        /** @} */
    };

}}

#endif // CONSOLE_ALARM_BAR_H
