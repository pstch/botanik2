#ifndef PAGES_SCROLLBACK_H
#define PAGES_SCROLLBACK_H

/**
 * @file scrollback.h
 * @brief Scrollback - a LineBuffer painted as a window, used by the logs,
 *        shell and home pages through the typedefs below.
 */

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "libbotanik/utils/minmax.h"
#include "libbotanik/config/terminal.h"

#include "libbotanik/console/terminal.h"
#include "libbotanik/pages/line_buffer.h"

namespace Botanik { namespace Pages {

    /** @brief How a stored line reaches the screen: PACED a few per paint, so
     *  a burst scrolls rather than jumps, or AT_ONCE. */
    enum class Reveal : uint8_t { PACED, AT_ONCE };

    /** @brief Which way the view moves through the history. */
    enum class Direction : uint8_t { OLDER, NEWER };

    /** @brief Circular scrollback buffer, painted as a window.
     *
     *  The window is bottom-aligned within its band and repainted whole
     *  whenever what it shows changed; the terminal finds the rows that only
     *  slid. New lines arriving while the page is active are revealed a few
     *  per paint, so a burst scrolls rather than jumps. */
    template<uint16_t L>
    class Scrollback : public LineBuffer<L, Config::SCROLLBACK_LINE_LEN> {
    private:
        using Base         = LineBuffer<L, Config::SCROLLBACK_LINE_LEN>;
        using AnsiTerminal = Terminal::AnsiTerminal;

        /** @name View state
         *  @{ */

        /** @brief Stored lines not revealed yet, the newest first. */
        uint16_t _pending = 0;
        /** @brief Lines the view is scrolled back from the revealed bottom;
         *  0 is the live tail. */
        uint16_t _view_offset = 0;
        /** @brief The width the stored lines were wrapped at; 0 while empty.
         *  @see reflow() */
        uint16_t _wrap_cols = 0;

        /** @name What the screen holds
         *  Where the window was painted and which line sat on its bottom row, so
         *  the next paint can say how far the lines moved. @{ */
        uint16_t _painted_first = 0, _painted_rows = 0;
        uint32_t _painted_bottom = 0;
        bool     _painted_valid = false;
        /** @} */

        /** @} */

        /** @brief @p n lines were stored: a live view reveals them at @p reveal,
         *  a scrolled-back one moves with them so it keeps showing the same
         *  lines. */
        void _stored(uint16_t n, Reveal reveal) {
            const uint16_t cnt = this->count();
            if (_view_offset != 0)
                _view_offset = (uint16_t)Utils::min<uint32_t>((uint32_t)_view_offset + n, cnt);
            else if (reveal == Reveal::PACED)
                _pending = (uint16_t)Utils::min<uint32_t>((uint32_t)_pending + n, cnt);
        }

    public:

        /** @name Line storage
         *  @{ */

        /** @brief Forget all stored lines and return to the live tail. */
        void clear() {
            Base::clear();
            _pending = _view_offset = _wrap_cols = 0;
            _painted_valid = false;
        }

        /** @brief Drop the history if it was wrapped at a different width.
         *  The wrap is baked into what is stored, one entry per display row, so
         *  a narrower screen would clip every retained line rather than re-wrap
         *  it. @see dev_screen
         *  @note Idempotent: the common call changes nothing. */
        void reflow(uint16_t cols) {
            if (cols == 0 || cols == _wrap_cols) return;
            if (_wrap_cols != 0) clear();
            _wrap_cols = cols;
        }

        /** @brief The @p rows rows under the painted band already show the
         *  @p rows lines stored last: an input line echoed where it was typed.
         *  The next paint then moves them with the band instead of writing
         *  them again. */
        void adopt(uint16_t rows) {
            if (!_painted_valid || _painted_rows == 0) return;
            _painted_rows   = (uint16_t)(_painted_rows + rows);
            _painted_bottom += rows;
        }

        /** @brief Store one display row, shown at once. */
        void store(const char* buf, size_t len) {
            Base::store(buf, len);
            _stored(1, Reveal::AT_ONCE);
        }

        /** @brief Bank a (possibly wrapping) line. @return rows stored. */
        uint16_t ingest(const char* buf, size_t len, uint16_t cols,
                        const char* prefix, uint16_t plen, Reveal reveal) {
            reflow(cols);
            const uint16_t n = this->store_wrapped(buf, len, cols, prefix, plen);
            _stored(n, reveal);
            return n;
        }

        /** @} */

        /** @name View
         *  @{ */

        /** @brief Show the newest lines, everything revealed. */
        void reset_view() { _pending = _view_offset = 0; }

        /** @brief Move the view @p n lines @p towards over a band of @p rows,
         *  clamped to the history. @return whether it moved. */
        bool scroll(Direction towards, uint16_t n, uint16_t rows) {
            _pending = 0;           // scrolling reveals whatever was pending
            const uint16_t cnt  = this->count();
            const uint16_t max  = cnt > rows ? (uint16_t)(cnt - rows) : 0;
            const uint16_t from = Utils::min(_view_offset, max);
            const uint16_t want = (towards == Direction::OLDER)
                                ? (uint16_t)Utils::min<uint32_t>((uint32_t)from + n, max)
                                : (uint16_t)(from > n ? from - n : 0);
            if (want == _view_offset) return false;
            _view_offset = want;
            return true;
        }

        /** @brief Reveal up to LOG_ROWS_PER_TICK of the lines still pending.
         *  The owning page calls this once per tick(); calling it twice in one
         *  tick doubles the reveal rate. */
        void reveal_step() {
            _pending = (uint16_t)(_pending - Utils::min<uint16_t>(_pending, (uint16_t)Config::LOG_ROWS_PER_TICK));
        }

        /** @brief Paint the window into rows [@p first_row, @p first_row +
         *  @p rows). While scrolled back, a reverse-video count of the newer
         *  lines tops the window. */
        void paint(AnsiTerminal& t, uint16_t first_row, uint16_t rows) {
            _hint_move(t, first_row, rows);
            t.set_inverted(false);
            for (uint16_t i = 0; i < rows; i++) {
                // The bottom row shows the newest revealed line, 1 step back.
                const uint16_t from_end = (uint16_t)(rows - i + _pending + _view_offset);
                uint16_t len = 0;
                const char* ln = this->line(from_end, len);
                t.move_to({(uint16_t)(first_row + i), 0});
                if (ln) t.write_chars(ln, Utils::min(len, t.cols()));
                t.clear_to_eol();
            }
            if (_view_offset == 0 || rows == 0) return;
            char tag[20];
            const int n = snprintf(tag, sizeof(tag), " v %u more ", (unsigned)_view_offset);
            if (n <= 0 || t.cols() < (uint16_t)n) return;
            t.move_to({first_row, (uint16_t)(t.cols() - n)});
            t.set_inverted(true);
            t.write_chars(tag, (uint16_t)n);
            t.set_inverted(false);
        }

        /** @} */

    private:
        /** @brief Say how far the window's lines moved since the last paint.
         *
         *  A line sits at `first_row + rows - 1 - (bottom - line)`, so every line
         *  in the window moves by the same rows when the band or the newest line
         *  shown changes: a line arriving, a scroll back, a band resized. */
        void _hint_move(AnsiTerminal& t, uint16_t first_row, uint16_t rows) {
            const uint32_t bottom = this->serial() - _pending - _view_offset;
            if (_painted_valid && rows != 0 && _painted_rows != 0) {
                const int32_t delta = ((int32_t)first_row + rows) - ((int32_t)_painted_first + _painted_rows)
                                    - ((int32_t)bottom - (int32_t)_painted_bottom);
                // The union of the two bands, which a band resized needs; rows
                // adopt() took in are part of the old one. What did not move the
                // walk writes out.
                const uint16_t top = Utils::min(first_row, _painted_first);
                const uint16_t bot = Utils::max((uint16_t)(first_row + rows), (uint16_t)(_painted_first + _painted_rows));
                if (delta != 0 && bot > top)
                    t.change(Terminal::LayoutChange::scroll(top, (uint16_t)(bot - 1), (int16_t)delta));
            }
            _painted_first  = first_row;
            _painted_rows   = rows;
            _painted_bottom = bottom;
            _painted_valid  = true;
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
