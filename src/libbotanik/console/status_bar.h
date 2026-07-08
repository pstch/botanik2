// REVIEW_35: TODO
#ifndef CONSOLE_STATUS_BAR_H
#define CONSOLE_STATUS_BAR_H

/**
 * @file status_bar.h
 * @brief StatusBar - the terminal-owned row-0 chrome.
 *
 * Three fields on the reverse-video top row: page tabs (left), a centred
 * HH:MM:SS clock, and a right-aligned "PLC:xxx cpu:nnn% stk:nnn%" metrics field.
 * Owned by AnsiTerminal. The clock and metrics refresh on their own cadence,
 * each submitting a keyed TX_VALUE transaction (the scheduler paces + coalesces
 * them); per-cell diffing against the drawn cache means an unchanged field, or an
 * unchanged cell within a field, spends no TX bytes.
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

        /** @brief Visible width (+NUL) of "PLC:xxx cpu:nnn% stk:nnn% ". Build
         *  buffer and diff cache share this constant so they can't drift. */
        static constexpr uint16_t METRICS_LEN = 27;
        /** @brief Diff cache of the last *drawn* (emitted) metrics, and the
         *  content of the last submitted-but-not-yet-drawn transaction. The diff
         *  is against the drawn cache, so a coalesced update still covers every
         *  cell pending since the last draw; the drawn cache is promoted from the
         *  submitted one once the transaction has drained (tx_has_key). */
        char                _metrics_cache[METRICS_LEN] = {};
        char                _metrics_sub[METRICS_LEN]   = {};
        bool                _metrics_sub_v = false;
        /** @brief HH:MM:SS field width (no NUL). */
        static constexpr uint16_t CLOCK_LEN = 8;
        /** @brief Drawn / submitted clock caches (see the metrics pair). */
        char                _clock_cache[CLOCK_LEN] = {};
        char                _clock_sub[CLOCK_LEN]   = {};
        bool                _clock_sub_v = false;
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

        /** @brief Drop the clock and metrics diff caches so the next refresh
         *  repaints them in full (after a tx_discard on a page switch). */
        void invalidate();

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

        /** @brief Refresh the metrics field: if it changed, submit a coalescing
         *  transaction. @return true (kept for the caller's cadence bookkeeping). */
        bool update_metrics();

        /** @brief Refresh the centred HH:MM:SS clock the same way (skipped on a
         *  very narrow terminal). @return true if it submitted an update. */
        bool update_clock();

        /** @brief Render the right-side metrics field into @p buf (fixed width,
         *  so a byte index maps to a fixed column); returns its length. */
        int build_metrics(char* buf, size_t cap) const;

        /** @brief Submit the @p emit cells of @p field (@p hi inverted mask) at
         *  (row 0, @p col) as a @p key-coalesced transaction. The diff caches are
         *  managed by the callers (drawn-vs-submitted). */
        void submit_field(uint16_t col, const char* field, uint16_t len,
                          uint32_t hi, uint32_t key, uint32_t emit);

        /** @} */
    };

}}

#endif // CONSOLE_STATUS_BAR_H
