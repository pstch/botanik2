// REVIEW_35: TODO
#ifndef CONSOLE_ALARM_BAR_H
#define CONSOLE_ALARM_BAR_H

/**
 * @file alarm_bar.h
 * @brief AlarmBar - the terminal-owned bottom-row strip: " ALARMS : " then the
 *        unmasked, un-ack'd codes in normal video, so they read as a set.
 * @note Repainted only when that set changes or the row was wiped. No
 *       change-highlighting: the set is visible as a whole.
 */

#include <cstdint>

namespace Botanik { namespace Alarms { class AlarmController; } }

namespace Botanik { namespace Terminal {

    class AnsiTerminal;
    class Console;

    /** @brief Bottom-row alarm strip (see file header). */
    class AlarmBar {

        /** @name State
         *  @{ */

        /** @brief The console this bar belongs to (alarm source). */
        Console&            _console;
        /** @brief The VT device it paints through (cached _c.terminal()). */
        AnsiTerminal&       _terminal;
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

        /** @brief Per-step pass: repaint the bar when the set changed or the row was wiped.*/
        void step();

        /** @brief Flag a repaint (after a full-screen clear or page switch) */
        void mark_dirty() { _dirty = true; }

        /** @brief (Re-)declare the bar's row as chrome. The constructor does it
         *  once; a reconfigure() both MOVES the row and resets every row's
         *  class, so the frame's repaint has to say it again. */
        void claim_row();

        /** @} */

    private:
        /** @name Internals
         *  @{ */

        /** @brief The physical last row, reserved for the strip. */
        uint16_t _row() const;

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
