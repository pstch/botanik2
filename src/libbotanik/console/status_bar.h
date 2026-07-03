// REVIEW_35: TODO
#ifndef CONSOLE_STATUS_BAR_H
#define CONSOLE_STATUS_BAR_H

/**
 * @file status_bar.h
 * @brief StatusBar - the terminal-owned row-0 chrome.
 *
 * Three fields on the reverse-video top row: page tabs (left), a centred
 * HH:MM:SS clock, and a right-aligned "PLC:xxx cpu:nnn% stk:nnn%" metrics field.
 * Owned by AnsiTerminal and painted cooperatively over the active page each
 * step(): the clock and metrics share TX_STATUS_FIELDS_PER_TICK and the depth
 * floor, a round-robin cursor rotating which repaints first, and each field
 * diffs against its own cache so an unchanged field spends no TX budget.
 */

#include <cstddef>
#include <cstdint>

namespace Botanik { namespace Terminal {

    class AnsiTerminal;
    class Console;

    /** @brief Row-0 status bar (see file header). */
    class StatusBar {

        /** @name State
         *  @{ */

        /** @brief The console this bar belongs to (page state + context). */
        Console&            _c;
        /** @brief The VT device it paints through (cached _c.terminal()). */
        AnsiTerminal&       _t;
        /** @brief Last metrics refresh (wall clock). */
        uint64_t            _last_ms = 0;
        /** @brief Last clock refresh (uptime ms). */
        uint64_t            _clock_last_ms = 0;
        /** @brief Round-robin cursor over the two paced fields (0 = clock,
         *  1 = metrics) so a tight budget rotates which repaints first. */
        uint8_t             _field = 0;

        /** @brief Visible width (+NUL) of "PLC:xxx cpu:nnn% stk:nnn% ". Build
         *  buffer and diff cache share this constant so they can't drift. */
        static constexpr uint16_t METRICS_LEN = 27;
        /** @brief Diff cache of the last drawn metrics (zero-init forces first
         *  draw). */
        char                _metrics_cache[METRICS_LEN] = {};
        /** @brief HH:MM:SS field width (no NUL). */
        static constexpr uint16_t CLOCK_LEN = 8;
        /** @brief Diff cache of the last drawn clock. */
        char                _clock_cache[CLOCK_LEN] = {};

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

        /** @brief Repaint a single page tab in its active/inactive video, used
         *  by switch_to() to flip just the two tabs that change on a switch. */
        void draw_tab(uint8_t index);

        /** @brief Drop the clock and metrics diff caches so the next refresh
         *  repaints them in full (after a tx_discard on a page switch). */
        void invalidate();

        /** @} */

    private:
        /** @name Field painters
         *  @{ */

        /** @brief Refresh only the metrics field (skips if unchanged).
         *  @p restore_pos returns the cursor to where it started (a standalone
         *  periodic refresh); draw_row passes false. @return true once the field
         *  is in sync. */
        bool update_metrics(bool restore_pos = true);

        /** @brief Refresh the centred HH:MM:SS clock (skipped on a very narrow
         *  terminal). @p restore_pos as for update_metrics(). @return true if it
         *  emitted (a digit changed), to charge the field budget. */
        bool update_clock(bool restore_pos = true);

        /** @brief Render the right-side metrics field into @p buf (fixed width,
         *  so a byte index maps to a fixed column); returns its length. */
        int build_metrics(char* buf, size_t cap) const;

        /** @brief Paint the @p emit columns of @p field at (row 0, @p col) with
         *  @p hi as the inverted mask, snapshot it into @p cache, and restore the
         *  bar's video (and, when @p restore_pos, the cursor). */
        void paint_field(uint16_t col, const char* field, uint16_t len,
                         uint32_t emit, uint32_t hi, char* cache, bool restore_pos);

        /** @} */
    };

}}

#endif // CONSOLE_STATUS_BAR_H
