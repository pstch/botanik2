// REVIEW_35: TODO
#ifndef CONSOLE_STATUS_BAR_H
#define CONSOLE_STATUS_BAR_H

/**
 * @file status_bar.h
 * @brief StatusBar - the terminal-owned row-0 chrome.
 *
 * Three fields on the reverse-video top row: page tabs (left), a centred
 * HH:MM:SS clock, and a right-aligned "cpu:nnn% stk:nnn%" metrics field.
 * Owned by Console. The clock and metrics rebuild on their own cadence and
 * repaint their whole field; the terminal's back-buffer diff emits only the cells
 * that actually changed, so the bar keeps no diff cache.
 */

#include <cstddef>                    // size_t - the metric-string buffer capacities
#include <cstdint>                    // uint16_t/uint64_t - the tab columns and the clock stamp

namespace Botanik { namespace Terminal {

    class AnsiTerminal;
    class Console;

    /** @brief Row-0 status bar (see file header). */
    class StatusBar {

        /** @name State
         *  @{ */

        /** @brief The console this bar belongs to (page state + context). */
        Console&            _console;
        /** @brief The VT device it paints through (cached _console.terminal()). */
        AnsiTerminal&       _terminal;
        /** @brief Last refresh of the row (clock and metrics share the gate). */
        uint64_t            _last_ms = 0;

        /** @brief Visible width (+NUL) of "cpu:nnn% stk:nnn% ". */
        static constexpr uint16_t METRICS_LEN = 19;
        /** @brief HH:MM:SS field width (no NUL). */
        static constexpr uint16_t CLOCK_LEN = 8;
        /** @brief Column just past the last-drawn tab strip. The active tab is
         *  wider (N:NAME vs N), so the strip shrinks when switching to a
         *  shorter-named page; draw_tabs() clears the freed columns using this. */
        uint16_t            _tabs_end_col = 0;

        /** @} */

    public:
        /** @brief Bind the bar to its owning console (caches its device). */
        explicit StatusBar(Console& console);

        /** @name Cooperative paint
         *  @{ */

        /** @brief Per-tick pass: repaint the clock and/or metrics on their own
         *  cadence, within the shared per-tick field budget and the depth
         *  floor. @p now is the loop's uptime-ms reading. */
        void step(uint64_t now);

        /** @brief Full rebuild of the whole row (first paint, Ctrl-L redraw). */
        void draw_row();

        /** @brief Repaint the whole tab strip, used by switch_to(). The active
         *  tab's width changes on a switch (N:NAME vs N), shifting every tab
         *  after it, so the strip is redrawn as a unit (and shrink residue
         *  cleared) rather than patching individual tabs. */
        void draw_tabs();

        /** @} */

    private:
        /** @name Tab strip
         *  @{ */

        /** @brief Draw the compact tab strip (leading blank + one "N"/"N:NAME"
         *  tab per page, single-space separated) starting at the current cursor
         *  (row 0). Caller has forced the reverse-video background. @return the
         *  column just past the strip. */
        uint16_t draw_tab_strip();

        /** @} */

        /** @name Field painters
         *  @{ */

        /** @brief Repaint the metrics field (the diff drops the unchanged cells). */
        void update_metrics();

        /** @brief Refresh the centred HH:MM:SS clock the same way (skipped on a
         *  very narrow terminal). */
        void update_clock();

        /** @brief Render the right-side metrics field into @p buf (fixed width,
         *  so a byte index maps to a fixed column); returns its length. */
        int build_metrics(char* buf, size_t cap) const;

        /** @} */
    };

}}

#endif // CONSOLE_STATUS_BAR_H
