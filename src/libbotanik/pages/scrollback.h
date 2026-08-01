// REVIEW_35: TODO
#ifndef PAGES_SCROLLBACK_H
#define PAGES_SCROLLBACK_H

/**
 * @file scrollback.h
 * @brief Scrollback - a LineBuffer with an incremental redraw, used
 *        by the logs, shell and home pages (via the ShellScrollback /
 *        LogsScrollback / HomeScrollback typedefs below).
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "../../libbotanik_config.h"
#include "line_buffer.h"
#include "../console/terminal.h"

namespace Botanik { namespace Pages {

    /**
     * @brief Circular scrollback buffer with an incremental redraw.
     *
     * Layers a deferred redraw over a @ref LineBuffer of @p L lines: each log-line
     * row, DL/IL scroll and replay draw immediate-mode; the terminal back-buffer diff
     * pares the bytes.
     * The window is bottom-aligned within rows 1..log_rows (caller-supplied). Name
     * it through @ref ShellScrollback / @ref LogsScrollback / @ref HomeScrollback.
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

        /** @} */

        /** @name Live tail state
         *  @{ */

        /** @brief Stored lines not yet painted to the live tail. Bumped by
         *  note_pending() while active, drained by drain_tail(). Reset by
         *  begin_redraw()/clear() (a focus replay repaints the whole
         *  window). */
        uint16_t _pending = 0;

        /** @} */

        /** @name Scroll state
         *  @{ */

        /** @brief Lines the view is scrolled back from the live bottom
         *  (0 = newest). While > 0 the live tail is frozen (drain_tail()
         *  no-ops) so incoming lines don't yank the view; scroll() shifts it
         *  and scroll_to_bottom() releases it. Reset to 0 on
         *  begin_redraw()/clear() (a focus replay shows the newest). */
        uint16_t _view_offset = 0;

        /** @brief Offset last drawn by refresh_scroll_indicator(), so it emits
         *  the marker only when the view moves (not every tick). */
        uint16_t _indicator_offset = 0;

        /** @} */

        /** @name Paint (owner set by the page)
         *  These draw into the terminal's FRONT grid, never the wire: a scroll
         *  shifts the grid and the row paints stamp over it, so ordering within
         *  a tick is just program order. present() then emits the DL/IL and the
         *  minimal cell diff, paced under the tick's byte budget.
         *  @{ */

        /** @brief Draw log line @p from_newest (steps back from the newest
         *  stored line) at screen @p row; blank the row when the line is missing
         *  (past the stored set). */
        void _draw_line(AnsiTerminal& t, uint16_t row, uint16_t from_newest) {
            uint16_t llen = 0;
            const char* ln = this->line(from_newest, llen);
            t.move_to({row, 0});
            t.set_inverted(false);
            if (ln) t.write_chars(ln, (uint16_t)std::min((size_t)llen,
                                                         (size_t)t.cols()));
            t.clear_to_eol();
        }

        /** @brief Scroll the log band (emitted as a DL/IL by present()). */
        void _submit_scroll(AnsiTerminal& t, uint16_t first_row, uint16_t log_rows,
                            bool up, uint16_t n) {
            t.scroll_region(first_row, (uint16_t)(first_row + log_rows - 1), up, n);
        }

        /** @} */

    public:

        /** @name Line storage
         *  @{ */

        /** @brief Forget all stored lines and cancel any pending redraw. */
        void clear() {
            Base::clear();          // drop the stored lines
            _redraw_i = UINT16_MAX; // and cancel any pending replay
            _pending  = 0;          // nothing left to paint to the live tail
            _view_offset = 0;       // back to the live bottom
            _indicator_offset = 0;  // marker gone with the cleared screen
        }

        /** @} */

        /** @name Redraw
         *  @{ */

        /** @brief Stage a bottom-aligned incremental redraw within rows
         *         1..log_rows */
        void begin_redraw(uint16_t log_rows) {
            // The content area is cleared before focus()/redraw(); we only
            // stage the rows.
            _redraw_i    = UINT16_MAX; // cancel any pending drain
            // the replay repaints the whole visible window
            _pending     = 0;
            // a focus/redraw replay shows the newest lines
            _view_offset = 0;
            // the replay repaints the top row, clearing any marker
            _indicator_offset = 0;
            uint16_t cnt = this->count();
            if (cnt == 0 || log_rows == 0) return;

            _redraw_count     = (cnt < log_rows) ? cnt : log_rows;
            _redraw_first_row = (uint16_t)(log_rows - _redraw_count + 1);
            _redraw_i         = 0;
        }

        /** @brief Shared head of a log page's tick(): when @p active, drain a batch of any
         *  staged focus replay and report whether it is still going. The page returns early
         *  on true so the live-tail drain (a DL scroll) can't shift rows the replay already
         *  drew. No-op (returns false) on a background page. Same call sequence the pages
         *  open their tick() with - centralised here so the gate can't drift between them. */
        bool tick_redraw_gate(AnsiTerminal& terminal, bool active) {
            if (!active) return false;
            drain_redraw(terminal);
            return redraw_pending();
        }

        /** @brief True while a staged redraw still has rows left to emit */
        bool redraw_pending() const { return _redraw_i != UINT16_MAX; }

        /** @brief Draw every remaining replay row; present() paces the bytes,
         *  so there is no per-tick cap or cold-floor gate here. One-shot: after
         *  it runs the replay is fully staged. */
        void drain_redraw(AnsiTerminal& terminal) {
            if (_redraw_i == UINT16_MAX) return;
            while (_redraw_i < _redraw_count) {
                // Window row _redraw_i (0 = oldest shown) maps that many steps up
                // from the window's newest line, plus the scroll offset (0 for a
                // focus replay).
                _draw_line(terminal, (uint16_t)(_redraw_first_row + _redraw_i),
                           (uint16_t)(_redraw_count - _redraw_i + _view_offset));
                _redraw_i++;
            }
            _redraw_i = UINT16_MAX;
        }

        /** @} */

        /** @name Live tail
         *  @{ */

        /** @brief Scroll the log window up by @p n rows and paint the @p n
         *  newest stored lines at its bottom - the shared "DL the oldest rows
         *  at the top, IL the new ones at the bottom" step. The caller has
         *  banked the lines and handles TX back-pressure / cursor. @p first_row
         *  keeps static content above the window untouched (rows below shift up
         *  by DL and back down by IL). */
        void emit_tail(AnsiTerminal& terminal, uint16_t n,
                       uint16_t first_row, uint16_t log_rows,
                       uint16_t newest_from_end = 1) {
            if (log_rows == 0 || n == 0) return;
            // taller than the window: show newest rows
            if (n > log_rows) n = log_rows;

            // Scroll up by n: drops the n oldest visible rows and opens n blank
            // slots at the bottom, leaving rows above first_row and below the
            // window untouched. The row paints below then stamp over the slots.
            _submit_scroll(terminal, first_row, log_rows, /*up=*/true, n);
            uint16_t first_slot = (uint16_t)(first_row + log_rows - n);

            // Fill the slots top to bottom, oldest first. The batch's newest
            // line is @p newest_from_end back from the ring's newest
            // (default 1), so slot i gets the line
            // (newest_from_end + n - 1 - i) steps from newest.
            for (uint16_t i = 0; i < n; i++)
                _draw_line(terminal, (uint16_t)(first_slot + i),
                           (uint16_t)(newest_from_end + (n - 1) - i));
        }

        /** @brief Mark @p n freshly-stored lines as awaiting paint to the live
         *  tail. Clamped to the stored count so the watermark never points past
         *  the oldest line. Call only while active (a background page repaints
         *  via begin_redraw() on focus). */
        void note_pending(uint16_t n) {
            _pending = (uint16_t)(_pending + n);
            uint16_t cnt = this->count();
            if (_pending > cnt) _pending = cnt;
        }

        /** @brief Bank a (possibly wrapping) line and, when @p active, mark the
         *  rows pending for the paced tail drain. @return rows stored. */
        uint16_t ingest(const char* buf, size_t len, uint16_t cols,
                        const char* prefix, uint16_t plen, bool active) {
            uint16_t n = this->store_wrapped(buf, len, cols, prefix, plen);
            if (active) note_pending(n);
            return n;
        }

        /** @brief Paint up to LOG_ROWS_PER_TICK pending lines into the live-tail
         *  window, oldest first. The per-tick cap keeps the tail animating a few
         *  rows at a time (rather than jumping to the newest window on a burst);
         *  the scheduler paces the actual TX drain. @return true if it painted. */
        bool drain_tail(AnsiTerminal& terminal, uint16_t first_row,
                        uint16_t log_rows) {
            if (_view_offset != 0) return false;   // frozen while scrolled back
            if (_pending == 0) return false;
            uint16_t k = _pending;
            if (k > Config::LOG_ROWS_PER_TICK) k = Config::LOG_ROWS_PER_TICK;
            // The oldest unshown line is _pending back from newest; this
            // batch's newest is k-1 nearer.
            emit_tail(terminal, k, first_row, log_rows, (uint16_t)(_pending - k + 1));
            _pending = (uint16_t)(_pending - k);
            return true;
        }

        /** @} */

        /** @name Scrolling
         *  @{ */

        /** @brief Scroll the view by @p n lines (@p up = older, down = toward
         *  the live bottom), painting only the newly exposed rows (cheap, for a
         *  key-at-a-time scroll), clamped to history. No-op when everything
         *  fits, a replay is pending, or the view can't move. The live tail is
         *  frozen while scrolled up. @return true if the view moved. */
        bool scroll(AnsiTerminal& terminal, bool up, uint16_t n,
                    uint16_t first_row, uint16_t log_rows) {
            if (log_rows == 0 || n == 0 || redraw_pending()) return false;
            uint16_t cnt    = this->count();
            uint16_t maxoff = (cnt > log_rows) ? (uint16_t)(cnt - log_rows) : 0;
            if (maxoff == 0) return false;             // the whole buffer fits
            uint16_t want = up ? (uint16_t)(_view_offset + n)
                               : (uint16_t)(_view_offset > n ? _view_offset - n : 0);
            if (want > maxoff) want = maxoff;
            if (want == _view_offset) return false;    // already at that end
            uint16_t moved = up ? (uint16_t)(want - _view_offset)
                                : (uint16_t)(_view_offset - want);
            if (moved > log_rows) moved = log_rows;    // at most one screenful
            // Wipe any marker off the top row first: it overlays content there,
            // and the hardware scroll below would otherwise smear a stale copy
            // into the window (a ghost trail). refresh_scroll_indicator() redraws
            // it after the move. Repaint the line it sits on (oldest shown at the
            // current, pre-move offset).
            if (_indicator_offset != 0) {
                _draw_line(terminal, first_row, (uint16_t)(log_rows + _view_offset));
                _indicator_offset = 0;
            }
            _view_offset = want;
            // tail frozen until bottom
            _pending = 0;
            if (up) _emit_scroll_up(terminal, moved, first_row, log_rows);
            // Down toward newer is the live-tail motion: drop the top, append
            // the exposed lines at the bottom (offset+1 back from newest).
            else    emit_tail(terminal, moved, first_row, log_rows,
                              (uint16_t)(_view_offset + 1));
            // The "scrolled up" marker is repainted by refresh_scroll_indicator()
            // from the page tick (not here), so it survives unrelated repaints
            // and a hardware scroll can't strand a stale copy.
            return true;
        }


        /** @brief Keep the "scrolled up" marker in sync with the view, called
         *  each tick by the page. While scrolled back it overlays a compact
         *  reverse-video tag at the top-right of the window (how many newer
         *  lines lie below toward the live bottom); at the live bottom it shows
         *  nothing. Diffed against the last drawn offset so it emits only on a
         *  change, and saves/restores the cursor so it never disturbs the page.
         *
         *  Placed on the top window row: a scroll-down's hardware scroll carries
         *  it off the top (no stranded ghost), and the bottom-return replay
         *  repaints over it. */
        void refresh_scroll_indicator(AnsiTerminal& terminal,
                                      uint16_t first_row, uint16_t log_rows) {
            if (_view_offset == _indicator_offset) return;   // unchanged
            _indicator_offset = _view_offset;
            // Back at the live bottom: the scroll-off / replay already cleared
            // the tag, so there is nothing to draw.
            if (_view_offset == 0 || log_rows == 0) return;

            char tag[20];
            int n = snprintf(tag, sizeof(tag), " v %u more ", (unsigned)_view_offset);
            if (n <= 0) return;
            uint16_t taglen = (uint16_t)n;
            uint16_t cols   = terminal.cols();
            if (cols < taglen) return;

            // Drawn over the top row's content (after it in the page tick, so it
            // wins in the grid). present() re-parks the cursor, so no save/restore.
            terminal.move_to({first_row, (uint16_t)(cols - taglen)});
            terminal.set_inverted(true);
            terminal.write_chars(tag, taglen);
            terminal.set_inverted(false);
        }

        /** @} */

    private:
        /** @brief Shift the window down by @p n (older lines toward the top)
         *  and paint the n exposed top rows - the mirror of emit_tail's
         *  scroll-up.
         *  @p _view_offset must already hold the new offset. */
        void _emit_scroll_up(AnsiTerminal& terminal, uint16_t n,
                             uint16_t first_row, uint16_t log_rows) {
            if (log_rows == 0 || n == 0) return;
            if (n > log_rows) n = log_rows;

            _submit_scroll(terminal, first_row, log_rows, /*up=*/false, n);

            // Top row is the oldest shown: line(log_rows + _view_offset) back
            // from newest, each row down one nearer. The window is full here,
            // so the indices resolve.
            for (uint16_t i = 0; i < n; i++)
                _draw_line(terminal, (uint16_t)(first_row + i),
                           (uint16_t)(log_rows + _view_offset - i));
        }
    };

    /** @brief Shell page scrollback (input echo + interleaved log replay). */
    using ShellScrollback = Scrollback<Config::SHELL_SCROLLBACK_LINES>;
    /** @brief Logs page scrollback (live log tail replay). */
    using LogsScrollback  = Scrollback<Config::LOGS_SCROLLBACK_LINES>;
    /** @brief Home page scrollback - the small bottom-band log tail. */
    using HomeScrollback  = Scrollback<Config::HOME_SCROLLBACK_LINES>;

}}

#endif // PAGES_SCROLLBACK_H
