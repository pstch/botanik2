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

#include <cstdint>                          // uint16_t/uint32_t/uint64_t
#include <cstdio>                           // snprintf
#include <cstring>                          // memcpy / memset / strlen

#include "libbotanik/config/terminal.h"   // Config::SCREEN_* / EDITOR_* / SHELL_* / PAGE_*

#include "libbotanik/console/console.h"     // Console::rows / terminal
#include "libbotanik/console/field.h"       // Terminal::Field
#include "libbotanik/console/terminal.h"    // AnsiTerminal
#include "libbotanik/hal/platform.h"        // HAL::Clock::uptime_ms
#include "libbotanik/pages/page.h"          // Pages::Page
#include "libbotanik/utils/assert.h"        // BOTANIK_ASSERT

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
        /** @brief Last dynamic refresh (uptime ms). */
        uint64_t      _refresh_last_ms = 0;
        /** @brief Interval between full dynamic refreshes (ms); a subclass may
         *  lower it. */
        uint32_t      _refresh_ms      = Config::PAGE_COLD_MS;
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
        void _label(uint16_t row, const char* s) { _label_at(row, 2, s); }


        /** @brief Draw a dash-ruled group header "==== TITLE ====" padded to
         *  @p width. Not cached. */
        void _header(uint16_t row, uint16_t col, const char* title, uint16_t width);

        /** @brief Draw a static label at (@p row, @p col) without clear-to-EOL,
         *  so it never disturbs a second column on the same row. Not cached. */
        void _label_at(uint16_t row, uint16_t col, const char* s);

        /** @brief Render @p s as a fixed @p width-column value cell at (@p row,
         *  @p col), cached under @p slot, with watch(1) -d change highlighting.
         *  Space-padded (so it never touches an adjacent column) and diffed
         *  against the previous draw. Highlighting is per subfield: each digit
         *  run lights when any of its digits changed, cleared once it settles
         *  (first paint after a clear is never lit). The whole field is painted
         *  with the highlight mask; the terminal's back-buffer diff emits only
         *  the cells that actually moved. */
        void _cell(uint16_t slot, uint16_t row, uint16_t col, const char* s,
                   uint16_t width, bool highlight = true) {
            if (row >= _console.rows() || width == 0) return;
            // A slot past the cache renders uncached (no highlight): assert so a
            // site catches the overflow at bring-up. Hard cell limit is
            // CACHE_COLS-1 (31); a wider cell is silently clamped.
            BOTANIK_ASSERT(slot < CACHE_SLOTS);
            if (width > CACHE_COLS - 1) width = CACHE_COLS - 1;

            const bool     cached = (slot < CACHE_SLOTS);
            const uint32_t bit    = cached ? (1u << slot) : 0u;

            // Render into the fixed-width, space-padded field.
            char cf[CACHE_COLS];
            Terminal::Field::render(cf, s, width);

            const bool  fresh = !cached || (_vfresh & bit) != 0;
            const char* prev  = cached ? _vcache[slot] : "";

            // Per-column change mask -> whole-subfield highlight: a digit run
            // lights if any of its digits moved.
            uint32_t chg = Terminal::Field::changed(cf, prev, width, fresh), newhi = 0;
            if (!fresh && highlight) {
                for (uint16_t i = 0; i < width; ) {
                    if (cf[i] < '0' || cf[i] > '9') { ++i; continue; }
                    uint16_t a = i;
                    while (i < width && cf[i] >= '0' && cf[i] <= '9') ++i;
                    uint32_t run =
                        (((i - a) >= 32) ? ~0u : ((1u << (i - a)) - 1u)) << a;
                    if (chg & run) newhi |= run;   // any digit changed -> lit
                }
            }

            // Paint the whole field with the highlight mask; the terminal diff
            // drops the cells whose glyph and attribute both held.
            _terminal.paint_field_runs(row, col, cf, width, newhi);

            if (cached) {
                memcpy(_vcache[slot], cf, width);
                _vcache[slot][width] = '\0';
                _vfresh &= ~bit;
            }
        }


        /** @brief Format a ms uptime as "DDDd HH:MM:SS" (fixed-width fields, so
         *  each subfield keeps a stable column for per-subfield highlighting).
         *  13 columns wide, i.e. one page-column value field; past 999 days the
         *  day count grows a digit and the field with it. */
        static void fmt_uptime(char* buf, size_t cap, uint64_t ms);

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

        /** @brief Bind to a terminal. */
        MetricPage(Console& console)
            : _console(console), _terminal(console.terminal()) { _invalidate(); }

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

        void deactivate() override {}

        void focus() override {
            _invalidate();          // screen cleared on switch: repaint everything
            _repaint_all = true;    // tick() submits static + all values
        }

        void tick(bool active) override;

        /** @} */
    };

}}

#endif // PAGES_METRIC_PAGE_H
