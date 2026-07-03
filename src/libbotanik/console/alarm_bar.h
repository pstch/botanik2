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
 * and repainted cooperatively from step(): only when the unmasked set changes
 * or the row was wiped (mark_dirty()), and only while the TX ring is shallow.
 * No change-highlighting - the set is visible as a whole. The dirty flag
 * survives a deferred paint so a later tick retries.
 */

#include <cstdint>

namespace Botanik { namespace Alarms { class AlarmModel; } }

namespace Botanik { namespace Terminal {

    class AnsiTerminal;

    /** @brief Bottom-row alarm strip (see file header). */
    class AlarmBar {

        /** @name State
         *  @{ */

        /** @brief The terminal this bar paints through (alarm source, geometry,
         *  paint primitives). */
        AnsiTerminal&       _t;
        /** @brief Unmasked alarm set last painted (bit i = table row i). */
        uint64_t            _sig = 0;
        /** @brief A repaint is due (set on change / screen wipe). */
        bool                _dirty = true;

        /** @} */

    public:
        /** @brief Bind the bar to its owning terminal. */
        explicit AlarmBar(AnsiTerminal& terminal) : _t(terminal) {}

        /** @name Cooperative paint
         *  @{ */

        /** @brief Per-step pass: repaint when the unmasked set changed or the
         *  row was wiped, but only while the TX ring is shallow; saves and
         *  restores the page cursor and its visibility so the bar never steals a
         *  visible cursor (e.g. the shell editor's) to the bottom row. No-op
         *  without an alarm source. */
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
