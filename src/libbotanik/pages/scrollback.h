#ifndef PAGES_SCROLLBACK_H
#define PAGES_SCROLLBACK_H

/**
 * @file scrollback.h
 * @brief Scrollback - a LineBuffer painted as a window, used by the logs,
 *        shell and home pages through the typedefs below.
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "libbotanik/config/terminal.h"

#include "libbotanik/console/terminal.h"
#include "libbotanik/pages/line_buffer.h"

namespace Botanik { namespace Pages {

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

        /** @} */

        /** @brief @p n lines were stored: a live view reveals them paced, a
         *  scrolled-back one moves with them so it keeps showing the same lines. */
        void _stored(uint16_t n, bool paced) {
            const uint16_t cnt = this->count();
            if (_view_offset != 0)
                _view_offset = (uint16_t)std::min<uint32_t>((uint32_t)_view_offset + n, cnt);
            else if (paced)
                _pending = (uint16_t)std::min<uint32_t>((uint32_t)_pending + n, cnt);
        }

    public:

        /** @name Line storage
         *  @{ */

        /** @brief Forget all stored lines and return to the live tail. */
        void clear() {
            Base::clear();
            _pending = _view_offset = _wrap_cols = 0;
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

        /** @brief Store one display row, shown at once. */
        void store(const char* buf, size_t len) {
            Base::store(buf, len);
            _stored(1, /*paced=*/false);
        }

        /** @brief Bank a (possibly wrapping) line; @p active reveals its rows
         *  paced. @return rows stored. */
        uint16_t ingest(const char* buf, size_t len, uint16_t cols,
                        const char* prefix, uint16_t plen, bool active) {
            reflow(cols);
            const uint16_t n = this->store_wrapped(buf, len, cols, prefix, plen);
            _stored(n, active);
            return n;
        }

        /** @} */

        /** @name View
         *  @{ */

        /** @brief Show the newest lines, everything revealed. */
        void reset_view() { _pending = _view_offset = 0; }

        /** @brief Move the view @p n lines (@p up = older) over a band of
         *  @p rows, clamped to the history. @return whether it moved. */
        bool scroll(bool up, uint16_t n, uint16_t rows) {
            _pending = 0;           // scrolling reveals whatever was pending
            const uint16_t cnt  = this->count();
            const uint16_t max  = cnt > rows ? (uint16_t)(cnt - rows) : 0;
            const uint16_t from = std::min(_view_offset, max);
            const uint16_t want = up ? (uint16_t)std::min<uint32_t>((uint32_t)from + n, max)
                                     : (uint16_t)(from > n ? from - n : 0);
            if (want == _view_offset) return false;
            _view_offset = want;
            return true;
        }

        /** @brief Paint the window into rows [@p first_row, @p first_row +
         *  @p rows), revealing up to LOG_ROWS_PER_TICK pending lines first.
         *  While scrolled back, a reverse-video count of the newer lines tops
         *  the window. */
        void paint(AnsiTerminal& t, uint16_t first_row, uint16_t rows) {
            _pending = (uint16_t)(_pending - std::min<uint16_t>(_pending, (uint16_t)Config::LOG_ROWS_PER_TICK));
            t.set_inverted(false);
            for (uint16_t i = 0; i < rows; i++) {
                // The bottom row shows the newest revealed line, 1 step back.
                const uint16_t from_end = (uint16_t)(rows - i + _pending + _view_offset);
                uint16_t len = 0;
                const char* ln = this->line(from_end, len);
                t.move_to({(uint16_t)(first_row + i), 0});
                if (ln) t.write_chars(ln, std::min(len, t.cols()));
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
    };

    /** @brief Shell page scrollback (input echo + interleaved log replay). */
    using ShellScrollback = Scrollback<Config::SHELL_SCROLLBACK_LINES>;
    /** @brief Logs page scrollback (live log tail replay). */
    using LogsScrollback  = Scrollback<Config::LOGS_SCROLLBACK_LINES>;
    /** @brief Home page scrollback - the small bottom-band log tail. */
    using HomeScrollback  = Scrollback<Config::HOME_SCROLLBACK_LINES>;

}}

#endif // PAGES_SCROLLBACK_H
