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

        /** @name Live tail state
         *  @{ */

        /** @brief Stored lines not yet painted to the live tail. Bumped by
         *  note_pending() when a new line is banked while the page is active, drained
         *  by drain_tail(). Reset to 0 by begin_redraw()/clear() since a focus replay
         *  repaints the whole visible window. */
        uint16_t _pending = 0;

        /** @} */

        /** @name Scroll state
         *  @{ */

        /** @brief Lines the view is scrolled back from the live bottom (0 = newest).
         *  While > 0 the live tail is frozen (drain_tail() no-ops) so incoming lines
         *  don't yank the view; scroll() shifts it and scroll_to_bottom() releases it.
         *  Reset to 0 on begin_redraw()/clear() (a focus replay shows the newest). */
        uint16_t _view_offset = 0;

        /** @} */

    public:
        /** @name Line storage
         *  @{ */

        /** @brief Forget all stored lines and cancel any pending redraw. Storage
         *  (store/store_wrapped/line/count) is inherited from LineBuffer. */
        void clear() {
            Base::clear();          // drop the stored lines
            _redraw_i = UINT16_MAX; // and cancel any pending replay
            _pending  = 0;          // nothing left to paint to the live tail
            _view_offset = 0;       // back to the live bottom
        }

        /** @} */

        /** @name Redraw
         *  @{ */

        /** @brief Stage a bottom-aligned incremental redraw within the window
         *  rows @p first_row .. first_row+log_rows-1 (default first_row 1, the whole
         *  content area, reproduces the original rows 1..log_rows). */
        void begin_redraw(uint16_t log_rows, uint16_t first_row = 1) {
            // The content area is cleared by AnsiTerminal before focus()/redraw(),
            // so we only stage the rows to (re)draw here.
            _redraw_i    = UINT16_MAX; // cancel any pending drain
            _pending     = 0;          // the replay repaints the whole visible window
            _view_offset = 0;          // a focus/redraw replay shows the newest lines
            uint16_t cnt = this->count();
            if (cnt == 0 || log_rows == 0) return;

            _redraw_count     = (cnt < log_rows) ? cnt : log_rows;
            _redraw_first_row = (uint16_t)(first_row + (log_rows - _redraw_count));
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
                if (!terminal.tx_below_cold()) break;
                // Window row _redraw_i (0 = oldest shown) maps to the line that many
                // steps up from the newest line of the window, plus the scroll offset
                // (0 for a focus replay, > 0 when staged scrolled back).
                uint16_t llen = 0;
                const char* ln = this->line(
                    (uint16_t)(_redraw_count - _redraw_i + _view_offset), llen);
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
                       uint16_t first_row, uint16_t log_rows,
                       uint16_t newest_from_end = 1) {
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

            // Fill the slots top to bottom with this batch of n lines, oldest first.
            // The batch's newest line is @p newest_from_end back from the ring's newest
            // (default 1 = the live tail's most recent line), so slot i gets the line
            // (newest_from_end + n - 1 - i) steps from newest.
            for (uint16_t i = 0; i < n; i++) {
                uint16_t llen = 0;
                const char* ln = this->line((uint16_t)(newest_from_end + (n - 1) - i), llen);
                if (ln == nullptr) continue;
                terminal.move_to({(uint16_t)(first_slot + i), 0});
                uint16_t wlen = (uint16_t)std::min((size_t)llen, (size_t)terminal.cols());
                terminal.write_chars(ln, wlen);
                terminal.clear_to_eol();
            }
        }

        /** @brief Mark @p n freshly-stored lines as awaiting paint to the live tail.
         *  Clamped to the stored line count so the watermark never points past the
         *  oldest retained line. Call only while the page is active (a background page
         *  repaints from scratch via begin_redraw() on its next focus). */
        void note_pending(uint16_t n) {
            _pending = (uint16_t)(_pending + n);
            uint16_t cnt = this->count();
            if (_pending > cnt) _pending = cnt;
        }

        /** @brief Bank a (possibly wrapping) line and, when @p active, mark the
         *  produced rows pending for the paced tail drain. The one-stop entry the
         *  page log writers share. @return number of rows stored. */
        uint16_t ingest(const char* buf, size_t len, uint16_t cols,
                        const char* prefix, uint16_t plen, bool active) {
            uint16_t n = this->store_wrapped(buf, len, cols, prefix, plen);
            if (active) note_pending(n);
            return n;
        }

        /** @brief Lines stored but not yet painted to the live tail. */
        uint16_t pending() const { return _pending; }

        /** @brief Paint up to SCROLLBACK_ROWS_PER_TICK of the pending lines into the
         *  live-tail window (rows @p first_row .. first_row+log_rows-1), oldest first,
         *  only while the ring is shallow (cold gate). Scrolls the window up and
         *  appends the batch at the bottom via emit_tail(). The per-tick cap +
         *  depth gate keep a burst from flooding the TX ring.
         *  @return true if it painted (caller may then reposition its cursor). */
        bool drain_tail(AnsiTerminal& terminal, uint16_t first_row, uint16_t log_rows) {
            if (_view_offset != 0) return false;   // frozen while scrolled back
            if (_pending == 0) return false;
            if (!terminal.tx_below_cold()) return false;
            uint16_t k = _pending;
            if (k > Config::LOG_ROWS_PER_TICK) k = Config::LOG_ROWS_PER_TICK;
            // The oldest unshown line is _pending back from newest; this batch's newest
            // is k-1 nearer than that.
            emit_tail(terminal, k, first_row, log_rows, (uint16_t)(_pending - k + 1));
            _pending = (uint16_t)(_pending - k);
            return true;
        }

        /** @} */

        /** @name Scrolling
         *  @{ */

        /** @brief Scroll the view by @p n lines within the window (rows @p first_row
         *  .. first_row+log_rows-1). @p up moves toward older lines; down toward the
         *  live bottom. Shifts the window in place and paints only the newly exposed
         *  rows (cheap, for a key-at-a-time scroll), clamped to the stored history.
         *  No-op (returns false) when everything already fits, a replay is pending, or
         *  the view can't move further. While scrolled up the live tail is frozen.
         *  @return true if the view moved. */
        bool scroll(AnsiTerminal& terminal, bool up, uint16_t n,
                    uint16_t first_row, uint16_t log_rows) {
            if (log_rows == 0 || n == 0 || redraw_pending()) return false;
            uint16_t cnt    = this->count();
            uint16_t maxoff = (cnt > log_rows) ? (uint16_t)(cnt - log_rows) : 0;
            if (maxoff == 0) return false;             // the whole buffer fits on screen
            uint16_t want = up ? (uint16_t)(_view_offset + n)
                               : (uint16_t)(_view_offset > n ? _view_offset - n : 0);
            if (want > maxoff) want = maxoff;
            if (want == _view_offset) return false;    // already at the end in that direction
            uint16_t moved = up ? (uint16_t)(want - _view_offset)
                                : (uint16_t)(_view_offset - want);
            if (moved > log_rows) moved = log_rows;    // can't reveal more than a screenful
            _view_offset = want;
            _pending = 0;                              // tail stays frozen until back at bottom
            if (up) _emit_scroll_up(terminal, moved, first_row, log_rows);
            // Scrolling down toward newer is the same motion as the live tail: drop the
            // top, append the newly exposed lines at the bottom (offset+1 back from newest).
            else    emit_tail(terminal, moved, first_row, log_rows, (uint16_t)(_view_offset + 1));
            return true;
        }

        /** @brief Release a scrolled-back view: jump to the live bottom and stage a
         *  bottom-aligned replay (paced by drain_redraw). No-op when already at bottom. */
        void scroll_to_bottom(uint16_t log_rows) {
            if (_view_offset == 0) return;
            begin_redraw(log_rows);   // resets the offset and stages the newest window
        }

        /** @brief Lines the view is scrolled back from the live bottom (0 = newest). */
        uint16_t scroll_offset() const { return _view_offset; }

        /** @} */

    private:
        /** @brief Shift the window down by @p n (older lines toward the top) and paint
         *  the n newly exposed top rows. DL n at the window bottom then IL n at its top
         *  so rows above first_row and below the window net untouched (the mirror of
         *  emit_tail's scroll-up). @p _view_offset must already hold the new offset. */
        void _emit_scroll_up(AnsiTerminal& terminal, uint16_t n,
                             uint16_t first_row, uint16_t log_rows) {
            if (log_rows == 0 || n == 0) return;
            if (n > log_rows) n = log_rows;

            terminal.move_to({(uint16_t)(first_row + log_rows - n), 0});
            terminal.delete_rows(n);
            terminal.move_to({first_row, 0});
            terminal.insert_rows(n);

            // Top row is the oldest shown: line(log_rows + _view_offset) back from
            // newest; each row down is one nearer. The window is full here (maxoff > 0
            // implies count > log_rows), so these indices always resolve.
            for (uint16_t i = 0; i < n; i++) {
                uint16_t llen = 0;
                const char* ln = this->line((uint16_t)(log_rows + _view_offset - i), llen);
                terminal.move_to({(uint16_t)(first_row + i), 0});
                if (ln) {
                    uint16_t wlen = (uint16_t)std::min((size_t)llen, (size_t)terminal.cols());
                    terminal.write_chars(ln, wlen);
                }
                terminal.clear_to_eol();
            }
        }
    };

    /** @brief Shell page scrollback (input echo + interleaved log replay). */
    using ShellScrollback = Scrollback<Config::SHELL_SCROLLBACK_LINES>;
    /** @brief Logs page scrollback (live log tail replay). */
    using LogsScrollback  = Scrollback<Config::LOGS_SCROLLBACK_LINES>;
    /** @brief Home page scrollback - the small bottom-band log tail. */
    using HomeScrollback  = Scrollback<Config::HOME_SCROLLBACK_LINES>;
    /** @brief Alerts page scrollback - the scrollable alert table. */
    using AlertsScrollback = Scrollback<Config::ALERTS_SCROLLBACK_LINES>;

}}

#endif // PAGES_SCROLLBACK_H
