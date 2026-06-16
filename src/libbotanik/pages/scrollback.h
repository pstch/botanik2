#ifndef PAGES_SCROLLBACK_H
#define PAGES_SCROLLBACK_H

/**
 * @file scrollback.h
 * @brief Scrollback - a LineBuffer with throttled incremental redraw, used by
 *        the logs and shell pages (via the ShellScrollback / LogsScrollback
 *        typedefs below).
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "libbotanik_config.h"
#include "libbotanik/pages/line_buffer.h"
#include "libbotanik/terminal/terminal.h"

namespace Botanik { namespace Pages {

    /**
     * @brief Circular scrollback buffer with throttled, incremental redraw.
     *
     * Layers a deferred, throttled redraw over a @ref LineBuffer of @p L lines: it
     * stores recent display lines for replay on redraw/page-switch, emitting at most
     * SCROLLBACK_ROWS_PER_TICK rows per drain_redraw() so a full replay can't
     * flood TX in one tick.
     *
     * The window is bottom-aligned within rows 1..log_rows (caller-supplied, since
     * pages differ). Line storage is inherited from LineBuffer. Name it through
     * @ref ShellScrollback / @ref LogsScrollback rather than directly.
     */
    template<uint16_t L>
    class Scrollback : public LineBuffer<L, Config::SCROLLBACK_LINE_LEN> {
    private:
        using Base         = LineBuffer<L, Config::SCROLLBACK_LINE_LEN>;
        using AnsiTerminal = Terminal::AnsiTerminal;
        using CursorPos    = Terminal::Ansi::CursorPos;

        /** @name Deferred redraw state
         *  @{ */

        /** @brief Current redraw cursor; UINT16_MAX = no redraw pending. */
        uint16_t  _redraw_i         = UINT16_MAX;
        /** @brief Number of rows the staged redraw still replays. */
        uint16_t  _redraw_count     = 0;
        /** @brief First screen row the staged window draws to. */
        uint16_t  _redraw_first_row = 0;
        /** @brief Cursor to restore once the replay drains. */
        CursorPos _redraw_saved     = {0, 0};

        /** @} */

    public:
        /** @name Line storage
         *  @{ */

        /** @brief Forget all stored lines and cancel any pending redraw. Storage
         *  (store/store_wrapped/line/count) is inherited from LineBuffer. */
        void clear() {
            Base::clear();          // drop the stored lines
            _redraw_i = UINT16_MAX; // and cancel any pending replay
        }

        /** @} */

        /** @name Redraw
         *  @{ */

        /** @brief Stage a bottom-aligned incremental redraw within rows 1..log_rows */
        void begin_redraw(uint16_t log_rows) {
            // The content area is cleared by AnsiTerminal before focus()/redraw(),
            // so we only stage the rows to (re)draw here.
            _redraw_i = UINT16_MAX; // cancel any pending drain
            uint16_t cnt = this->count();
            if (cnt == 0 || log_rows == 0) return;

            _redraw_count     = (cnt < log_rows) ? cnt : log_rows;
            _redraw_first_row = (uint16_t)(log_rows - _redraw_count + 1);
            _redraw_i         = 0;
        }

        /** @brief True while a staged redraw still has rows left to emit */
        bool redraw_pending() const { return _redraw_i != UINT16_MAX; }

        /** @brief Cancel a staged redraw without emitting its remaining rows */
        void cancel_redraw() { _redraw_i = UINT16_MAX; }

        /** @brief Emit up to SCROLLBACK_ROWS_PER_TICK staged rows, stopping
         *         early if the TX buffer can't fit another row. No-op when idle. */
        void drain_redraw(AnsiTerminal& terminal) {
            if (_redraw_i == UINT16_MAX) return;
            // Capture the cursor once at the start of the replay, to restore when it
            // finishes rather than bouncing back every tick.
            if (_redraw_i == 0) _redraw_saved = terminal.cursor_pos();
            // Emit a small batch per call, stopping early if a worst-case row won't
            // fit, so the replay can't overrun TX in one tick.
            for (uint16_t k = 0; k < Config::SCROLLBACK_ROWS_PER_TICK
                                 && _redraw_i != UINT16_MAX; k++) {
                if (terminal.tx_available() < (uint16_t)(terminal.cols() + 32)) break;
                // Window row _redraw_i (0 = oldest shown) maps that many steps up
                // from the newest line of the window.
                uint16_t llen = 0;
                const char* ln = this->line((uint16_t)(_redraw_count - _redraw_i), llen);
                terminal.move_to({(uint16_t)(_redraw_first_row + _redraw_i), 0});
                if (ln) {
                    uint16_t write_len = (uint16_t)std::min((size_t)llen, (size_t)terminal.cols());
                    terminal.write_chars(ln, write_len);
                }
                terminal.clear_to_eol();
                if (++_redraw_i >= _redraw_count) _redraw_i = UINT16_MAX;
            }
            // Restore the cursor only once the whole replay drained; the page holds
            // off other output while it's pending, and the next batch repositions absolutely.
            if (_redraw_i == UINT16_MAX) terminal.move_to(_redraw_saved);
        }

        /** @} */

        /** @name Live tail
         *  @{ */

        /** @brief Scroll the log window (rows @p first_row .. first_row+log_rows-1)
         *         up by @p n rows and paint the @p n newest stored lines at its bottom.
         *
         * The shared "DL the oldest rows at the top, IL the new ones at the bottom"
         * step for every live-tail page. The caller has already banked the lines and
         * handles TX back-pressure and cursor repositioning. @p first_row lets a page
         * keep static content above the window untouched; rows below it are shifted
         * up by the DL and back down by the IL, so they end up untouched too. */
        void emit_tail(AnsiTerminal& terminal, uint16_t n,
                       uint16_t first_row, uint16_t log_rows) {
            if (log_rows == 0 || n == 0) return;
            if (n > log_rows) n = log_rows;  // taller than the window: show newest rows

            // DL n at the window top: drop the n oldest visible rows, shifting the
            // window (and anything below it) up by n. Rows above first_row are left
            // untouched by the DL.
            terminal.move_to({first_row, 0});
            terminal.delete_rows(n);

            // IL n at the window's bottom: push whatever sits below back down,
            // leaving n blank slots for the incoming lines.
            uint16_t first_slot = (uint16_t)(first_row + log_rows - n);
            terminal.move_to({first_slot, 0});
            terminal.insert_rows(n);

            // Fill the slots with the n newest stored lines, top to bottom.
            for (uint16_t i = 0; i < n; i++) {
                uint16_t llen = 0;
                const char* ln = this->line((uint16_t)(n - i), llen);
                if (ln == nullptr) continue;
                terminal.move_to({(uint16_t)(first_slot + i), 0});
                uint16_t wlen = (uint16_t)std::min((size_t)llen, (size_t)terminal.cols());
                terminal.write_chars(ln, wlen);
                terminal.clear_to_eol();
            }
        }

        /** @} */
    };

    /** @brief Shell page scrollback (input echo + interleaved log replay). */
    using ShellScrollback = Scrollback<Config::SHELL_SCROLLBACK_LINES>;
    /** @brief Logs page scrollback (live log tail replay). */
    using LogsScrollback  = Scrollback<Config::LOGS_SCROLLBACK_LINES>;
    /** @brief Home page scrollback - the small bottom-band log tail. */
    using HomeScrollback  = Scrollback<Config::HOME_SCROLLBACK_LINES>;

}}

#endif // PAGES_SCROLLBACK_H
