// REVIEW_35: TODO
#ifndef PAGES_METRIC_PAGE_H
#define PAGES_METRIC_PAGE_H

/**
 * @file metric_page.h
 * @brief MetricPage - shared base for read-only metric displays (Home/Status).
 *
 * Shares the terminal binding, focus/periodic-refresh lifecycle, and drawing
 * helpers. Fields are drawn immediate-mode; the terminal's back-buffer diff
 * emits only the cells that moved. A per-slot value cache is kept solely to drive
 * the change highlight (which must know what changed since last refresh), not for
 * byte-level diffing.
 *
 * A changed digit run is drawn reverse-video and cleared once it settles -
 * watch(1) -d highlighting. Fields are fixed-width space-padded, so each subfield
 * keeps a stable column.
 *
 * Subclasses implement draw_static() (labels/headers, once on focus) and
 * draw_dynamic() (values, per refresh).
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "libbotanik_config.h"
#include "libbotanik/utils/assert.h"       // BOTANIK_ASSERT (DEBUG slot guard)
#include "libbotanik/hal/platform.h"
#include "libbotanik/pages/page.h"
#include "libbotanik/console/console.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/console/field.h"
#include "libbotanik/plcio/sync.h"

namespace Botanik { namespace Pages {

    /** @brief Base for diff-rendered metric pages (see file header). */
    class MetricPage : public Page {
    protected:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Console      = Terminal::Console;
        using Command      = Terminal::InputEvent::Command;

        /** @name State
         *  @{ */

        /** @brief Owning console (content-area geometry + context). */
        Console&      _console;
        /** @brief The VT device it paints through (cached _console.terminal()). */
        AnsiTerminal& _terminal;
        /** @brief Column at which values are written (labels start at column
         *  2). */
        uint16_t      _value_col;
        /** @brief True while this page owns the screen. */
        bool          _active          = false;
        /** @brief Last dynamic refresh (uptime ms). */
        uint64_t      _refresh_last_ms = 0;
        /** @brief Interval between full dynamic refreshes (ms); a subclass may
         *  lower it. */
        uint32_t      _refresh_ms      = Config::PAGE_REFRESH_MS;
        /** @brief Last hot-field refresh (uptime ms). */
        uint64_t      _hot_last_ms     = 0;
        /** @brief Hot-field refresh interval (ms); 0 disables. Paired with
         *  draw_hot() for fields that change every second (e.g. a clock); a hot
         *  field repaints at most once per cycle (see tick()). Cold fields live
         *  in draw_dynamic(). */
        uint32_t      _hot_ms          = 0;
        /** @brief Set on focus (a cleared screen): the next tick draws the
         *  static chrome and repaints every value. */
        bool          _repaint_all       = false;
        /** @brief A forced value refresh is due (ENTER), independent of the
         *  periodic cadence. */
        bool          _cold_due          = false;

        /** @brief Diff-cache slots and per-slot value width. Single-column
         *  pages key a slot by row; column layouts (_cell()) pass an explicit
         *  slot so two cells share a row. CACHE_COLS is 32 so a per-column mask
         *  fits a uint32. */
        static constexpr uint16_t CACHE_SLOTS = 32;
        static constexpr uint16_t CACHE_COLS  = 32;
        static_assert(CACHE_COLS  <= 32,
                      "CACHE_COLS must fit a uint32 per-column mask");
        static_assert(CACHE_SLOTS <= 32,
                      "CACHE_SLOTS must fit a uint32 per-slot mask");
        /** @brief Last value drawn per slot: the previous value a refresh diffs
         *  against to drive the watch(1)-style change highlight. (The terminal's
         *  back-buffer diff handles the byte-level "emit only what moved"; this
         *  cache exists only for the highlight, which needs to know what changed.) */
        char          _vcache[CACHE_SLOTS][CACHE_COLS];
        /** @brief Slots awaiting a first paint after a clear (bit i = slot i):
         *  drawn unhighlighted (nothing has "changed" yet). */
        uint32_t      _vfresh = ~0u;

        /** @} */

        /** @name Drawing helpers
         *  @{ */

        /** @brief Forget every cached value so the next pass repaints in full
         *  (on focus, screen just cleared). Marking all slots fresh keeps the
         *  repaint unhighlighted. */
        void _invalidate() {
            memset(_vcache, 0, sizeof(_vcache));
            _vfresh = ~0u;
        }

        /** @brief Draw a label at column 2. Only
         *  its own glyphs are written - no clear-to-EOL: a label is always drawn
         *  onto a freshly-cleared screen (focus / Ctrl-L), so a clear would be
         *  redundant (and would wipe an adjacent block). Not value-cached. */
        void _label(uint16_t row, const char* s) {
            if (row >= _console.rows()) return;
            _terminal.move_to({row, 2});
            _terminal.set_inverted(false);
            _terminal.write_chars(s, (uint16_t)strlen(s));
        }

        /** @brief Write a value at _value_col. The field runs to
         *  EOL, so a shrinking value clears its own tail. @p highlight = false
         *  for fields that change every refresh (a clock). @p invert lights the
         *  whole value as one block on change (for non-numeric values like a
         *  status label) instead of per-digit-run. */
        void _value(uint16_t row, const char* s, bool highlight = true,
                    bool invert = false) {
            if (row >= _console.rows()) return;
            uint16_t width = (_value_col < _terminal.cols())
                                 ? (uint16_t)(_terminal.cols() - _value_col) : 0;
            _field(row, row, _value_col, s, width, highlight, invert);
        }

        /** @brief Draw a dash-ruled group header "==== TITLE ====" padded to
         *  @p width. Not cached. */
        void _header(uint16_t row, uint16_t col, const char* title, uint16_t width) {
            if (row >= _console.rows()) return;
            char buf[96];
            if (width > (uint16_t)(sizeof(buf) - 1))
                width = (uint16_t)(sizeof(buf) - 1);
            uint16_t n = 0;
            for (int i = 0; i < 4 && n < width; ++i) buf[n++] = '=';
            if (n < width) buf[n++] = ' ';
            for (const char* p = title; *p && n < width; ++p) buf[n++] = *p;
            if (n < width) buf[n++] = ' ';
            while (n < width) buf[n++] = '=';
            _terminal.move_to({row, col});
            _terminal.set_inverted(false);
            _terminal.write_chars(buf, n);
        }

        /** @brief Draw a static label at (@p row, @p col) without clear-to-EOL,
         *  so it never disturbs a second column on the same row. Not cached. */
        void _label_at(uint16_t row, uint16_t col, const char* s) {
            if (row >= _console.rows()) return;
            _terminal.move_to({row, col});
            _terminal.set_inverted(false);
            _terminal.write_chars(s, (uint16_t)strlen(s));
        }

        /** @brief Diff-rendered value cell for column layouts: write @p s
         *  padded/truncated to @p width (never touching an adjacent column).
         *  Cached by @p slot. */
        void _cell(uint16_t slot, uint16_t row, uint16_t col, const char* s,
                   uint16_t width, bool highlight = true) {
            _field(slot, row, col, s, width, highlight);
        }

        /** @brief Render @p s as a fixed @p width-column field, cached under
         *  @p slot, with watch(1) -d change highlighting. Space-padded, diffed
         *  against the previous draw so only changed bytes re-emit.
         *  Highlighting is per subfield: each digit run lights when any of its
         *  digits changed, cleared once it settles (first paint after a clear is
         *  never lit). @p invert instead lights the whole value as a block on any
         *  change. The whole field is painted with the highlight mask; the
         *  terminal's back-buffer diff emits only the cells that actually moved. */
        void _field(uint16_t slot, uint16_t row, uint16_t col, const char* s,
                    uint16_t width, bool highlight = true, bool invert = false) {
            if (row >= _console.rows() || width == 0) return;
            // A slot past the cache renders uncached (no highlight): assert so a
            // site catches the overflow at bring-up. Hard cell limit is
            // CACHE_COLS-1 (31); a wider cell is silently clamped.
            BOTANIK_ASSERT(slot < CACHE_SLOTS);
            if (width > CACHE_COLS - 1) width = CACHE_COLS - 1;

            const bool     cached = (slot < CACHE_SLOTS);
            const uint32_t bit    = cached ? (1u << slot) : 0u;

            // Render into the fixed-width, space-padded field.
            char     cf[CACHE_COLS];
            uint16_t vlen = Terminal::Field::render(cf, s, width);

            const bool  fresh = !cached || (_vfresh & bit) != 0;
            const char* prev  = cached ? _vcache[slot] : "";

            // Per-column change mask -> whole-subfield highlight: a digit run
            // lights if any of its digits moved.
            uint32_t chg = Terminal::Field::changed(cf, prev, width, fresh), newhi = 0;
            if (!fresh && highlight) {
                if (invert) {
                    // Whole-value invert: light the printed glyphs ([0, vlen)).
                    if (chg) newhi = (vlen >= 32) ? ~0u : ((1u << vlen) - 1u);
                } else {
                    for (uint16_t i = 0; i < width; ) {
                        if (cf[i] < '0' || cf[i] > '9') { ++i; continue; }
                        uint16_t a = i;
                        while (i < width && cf[i] >= '0' && cf[i] <= '9') ++i;
                        uint32_t run =
                            (((i - a) >= 32) ? ~0u : ((1u << (i - a)) - 1u)) << a;
                        if (chg & run) newhi |= run;   // any digit changed -> lit
                    }
                }
            }

            // Paint the whole field with the highlight mask; the terminal diff
            // drops the cells whose glyph and attribute both held.
            uint32_t all = (width >= 32) ? ~0u : (uint32_t)((1u << width) - 1u);
            _terminal.paint_field_runs(row, col, cf, width, all, newhi);

            if (cached) {
                memcpy(_vcache[slot], cf, width);
                _vcache[slot][width] = '\0';
                _vfresh &= ~bit;
            }
        }

        /** @brief Map a PLC link status to a short display string. */
        static const char* plc_str(PlcIO::PlcStatus s) {
            switch (s) {
                case PlcIO::PLC_RUNNING: return "RUNNING";
                case PlcIO::PLC_TIMEOUT: return "TIMEOUT";
                case PlcIO::PLC_FAULT:   return "FAULT";
                default:                 return "UNKNOWN";
            }
        }

        /** @brief Format a ms uptime as "DDDDd HH:MM:SS" (fixed-width fields,
         *  so each subfield keeps a stable column for per-subfield
         *  highlighting). */
        static void fmt_uptime(char* buf, size_t cap, uint64_t ms) {
            uint64_t s = ms / 1000;
            unsigned d   = (unsigned)(s / 86400); s %= 86400;
            unsigned h   = (unsigned)(s / 3600);  s %= 3600;
            unsigned m   = (unsigned)(s / 60);
            unsigned sec = (unsigned)(s % 60);
            snprintf(buf, cap, "%04ud %02u:%02u:%02u", d, h, m, sec);
        }

        /** @} */

        /** @name Subclass hooks
         *  @{ */

        /** @brief Draw labels/headers that only change on (re)entry/resize. */
        virtual void draw_static() = 0;
        /** @brief Rewrite the cold values (global refresh period). Must NOT
         *  touch hot fields, or they would paint twice per cycle and the
         *  highlight would flicker. */
        virtual void draw_dynamic() = 0;
        /** @brief Rewrite the hot values - the per-second subset. Default none;
         *  see _hot_ms. Kept disjoint from draw_dynamic(). */
        virtual void draw_hot() {}

        /** @} */

        /** @brief Bind to a terminal; @p value_col is where values are written. */
        MetricPage(Console& console, uint16_t value_col)
            : _console(console), _terminal(console.terminal()),
              _value_col(value_col) { _invalidate(); }

    public:
        /** @name Page interface
         *  @{ */

        void handle_text(char) override {}

        /** @brief ENTER forces an immediate value refresh (otherwise throttled
         *  to _refresh_ms); other keys ignored. The refresh is diff-gated, so it
         *  re-emits only fields that actually changed. */
        void handle_command(Command cmd, uint16_t = 1) override {
            if (cmd == Command::ENTER) _cold_due = true;
        }

        void deactivate() override { _active = false; }

        void focus() override {
            _active = true;
            _invalidate();          // screen cleared on switch: repaint everything
            _repaint_all = true;    // tick() submits static + all values
        }

        void tick(bool active) override {
            _active = active;
            if (!active) return;
            uint64_t now = HAL::Clock::uptime_ms();

            // Fresh screen: draw the static chrome, force every value fresh, then
            // paint hot + cold once.
            if (_repaint_all) {
                draw_static();
                _invalidate();
                draw_hot();
                draw_dynamic();
                _hot_last_ms     = now;
                _refresh_last_ms = now;
                _cold_due        = false;
                _repaint_all     = false;
                return;
            }

            // Hot values (e.g. a clock) on their own cadence; cold values on the
            // refresh period or a forced ENTER. Both diff-gated, so a steady value
            // spends no TX.
            if (_hot_ms && now - _hot_last_ms >= _hot_ms) {
                _hot_last_ms = now;
                draw_hot();
            }
            if (_cold_due || now - _refresh_last_ms >= _refresh_ms) {
                _refresh_last_ms = now;
                _cold_due = false;
                draw_dynamic();
            }
        }

        /** @} */
    };

}}

#endif // PAGES_METRIC_PAGE_H
