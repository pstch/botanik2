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
     * Layers a deferred redraw over a @ref LineBuffer of @p L lines: emits at
     * most SCROLLBACK_ROWS_PER_TICK rows per drain_redraw() so a full replay
     * can't flood TX in one tick. The window is bottom-aligned within rows
     * 1..log_rows (caller-supplied). Name it through @ref ShellScrollback /
     * @ref LogsScrollback.
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

        /** @brief Cancel a staged redraw without emitting its remaining rows */
        void cancel_redraw() { _redraw_i = UINT16_MAX; }

        /** @brief Emit up to SCROLLBACK_ROWS_PER_TICK staged rows, stopping
         *         early if the TX buffer can't fit another row. No-op when
         *         idle. */
        void drain_redraw(AnsiTerminal& terminal) {
            if (_redraw_i == UINT16_MAX) return;
            // Capture the cursor once at the start, restored when the replay
            // finishes (not bounced back every tick).
            if (_redraw_i == 0) _redraw_saved = terminal.cursor_pos();
            // A small batch per call, stopping early if a worst-case row won't
            // fit, so the replay can't overrun TX in one tick.
            for (uint16_t k = 0; k < Config::SCROLLBACK_ROWS_PER_TICK
                                 && _redraw_i != UINT16_MAX; k++) {
                if (!terminal.tx_below_cold()) break;
                // Window row _redraw_i (0 = oldest shown) maps that many steps
                // up from the window's newest line, plus the scroll offset
                // (0 for a focus replay).
                uint16_t llen = 0;
                const char* ln = this->line(
                    (uint16_t)(_redraw_count - _redraw_i + _view_offset), llen);
                terminal.move_to({(uint16_t)(_redraw_first_row + _redraw_i), 0});
                if (ln) {
                    uint16_t write_len =
                        (uint16_t)std::min((size_t)llen, (size_t)terminal.cols());
                    terminal.write_chars(ln, write_len);
                }
                terminal.clear_to_eol();
                if (++_redraw_i >= _redraw_count) _redraw_i = UINT16_MAX;
            }
            // Restore the cursor only once the whole replay drained (each batch
            // repositions absolutely, and the page holds off other output
            // meanwhile).
            if (_redraw_i == UINT16_MAX) terminal.move_to(_redraw_saved);
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

            // Scroll up by n: drops the n oldest visible rows, opens n blank
            // slots at the bottom, leaving rows above first_row and below the
            // window untouched.
            terminal.scroll_region(first_row, (uint16_t)(first_row + log_rows - 1),
                                   /*up=*/true, n);
            uint16_t first_slot = (uint16_t)(first_row + log_rows - n);

            // Fill the slots top to bottom, oldest first. The batch's newest
            // line is @p newest_from_end back from the ring's newest
            // (default 1), so slot i gets the line
            // (newest_from_end + n - 1 - i) steps from newest.
            for (uint16_t i = 0; i < n; i++) {
                uint16_t llen = 0;
                const char* ln =
                    this->line((uint16_t)(newest_from_end + (n - 1) - i), llen);
                if (ln == nullptr) continue;
                terminal.move_to({(uint16_t)(first_slot + i), 0});
                uint16_t wlen =
                    (uint16_t)std::min((size_t)llen, (size_t)terminal.cols());
                terminal.write_chars(ln, wlen);
                terminal.clear_to_eol();
            }
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

        /** @brief Lines stored but not yet painted to the live tail. */
        uint16_t pending() const { return _pending; }

        /** @brief Paint up to LOG_ROWS_PER_TICK pending lines into the
         *  live-tail window, oldest first, only while the ring is shallow
         *  (cold gate). The per-tick cap + depth gate keep a burst from
         *  flooding TX. @return true if it painted. */
        bool drain_tail(AnsiTerminal& terminal, uint16_t first_row,
                        uint16_t log_rows) {
            if (_view_offset != 0) return false;   // frozen while scrolled back
            if (_pending == 0) return false;
            if (!terminal.tx_below_cold()) return false;
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
                uint16_t elen = 0;
                const char* eln = this->line((uint16_t)(log_rows + _view_offset), elen);
                terminal.move_to({first_row, 0});
                if (eln)
                    terminal.write_chars(eln, (uint16_t)std::min((size_t)elen,
                                                                 (size_t)terminal.cols()));
                terminal.clear_to_eol();
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

        /** @brief Release a scrolled-back view: jump to the live bottom and
         *  stage a bottom-aligned replay (paced by drain_redraw). No-op when
         *  already at bottom. */
        void scroll_to_bottom(uint16_t log_rows) {
            if (_view_offset == 0) return;
            // resets the offset and stages the newest window
            begin_redraw(log_rows);
        }

        /** @brief Lines the view is scrolled back from the live bottom
         *         (0 = newest). */
        uint16_t scroll_offset() const { return _view_offset; }

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

            CursorPos saved = terminal.cursor_pos();
            terminal.move_to({first_row, (uint16_t)(cols - taglen)});
            terminal.set_inverted(true);
            terminal.write_chars(tag, taglen);
            terminal.set_inverted(false);
            terminal.move_to(saved);            // hand the cursor back
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

            terminal.scroll_region(first_row, (uint16_t)(first_row + log_rows - 1),
                                   /*up=*/false, n);

            // Top row is the oldest shown: line(log_rows + _view_offset) back
            // from newest, each row down one nearer. The window is full here,
            // so the indices resolve.
            for (uint16_t i = 0; i < n; i++) {
                uint16_t llen = 0;
                const char* ln =
                    this->line((uint16_t)(log_rows + _view_offset - i), llen);
                terminal.move_to({(uint16_t)(first_row + i), 0});
                if (ln) {
                    uint16_t wlen =
                        (uint16_t)std::min((size_t)llen, (size_t)terminal.cols());
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

}}

#endif // PAGES_SCROLLBACK_H
