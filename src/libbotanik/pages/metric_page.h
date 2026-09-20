#ifndef PAGES_METRIC_PAGE_H
#define PAGES_METRIC_PAGE_H

/**
 * @file metric_page.h
 * @brief MetricPage - shared base for read-only metric displays (Home/Status).
 *
 * Shares the terminal binding, focus/periodic-refresh lifecycle, and drawing
 * helpers. Fields are drawn immediate-mode and pared by the terminal's diff
 * (@ref dev_screen); the FRONT grid is read back only for the change
 * highlight, which needs to know WHAT changed, not which cells moved.
 *
 * A changed digit run is drawn reverse-video and cleared once it settles -
 * watch(1) -d highlighting. Fields are fixed-width space-padded, so each subfield
 * keeps a stable column.
 *
 * Subclasses implement draw_static() (labels/headers, once on focus) and
 * draw_dynamic() (values, per refresh).
 */

#include <cstdint>
#include <cstring>

#include "libbotanik/config/terminal.gen.h"

#include "libbotanik/console/console.h"
#include "libbotanik/console/field.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/console/page.h"
#include "libbotanik/utils/assert.h"

namespace Botanik { namespace Pages {

    /** @brief Base for diff-rendered metric pages (see file header). */
    class MetricPage : public Terminal::Page {
    protected:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Console      = Terminal::Console;
        using Command      = Terminal::InputEvent::Command;

        /** @brief Whether a value cell lights the digits that changed. A cell
         *  whose value moves every beat is PLAIN: the highlight would be
         *  permanent and say nothing. */
        enum class Highlight : uint8_t { LIT, PLAIN };

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
         *  @{ */

        /** @brief Widest value cell. The bound is the per-column highlight
         *  mask, which is one uint32. */
        static constexpr uint16_t FIELD_MAX = 32;

        /** @brief Whether the next values drawn are the first since the screen
         *  was cleared. Nothing is highlighted on such a frame: there is no
         *  previous value to have changed from.
         *  @note One flag rather than a mask per cell, every cell being painted
         *  on the repaint frame. @see _cell */
        bool          _fresh = true;

        /** @} */

        /** @name Drawing helpers
         *  @{ */

        /** @brief Declare the next pass a repaint onto a cleared screen, so it
         *  draws unhighlighted. Cleared once that pass has run. */
        void _invalidate() { _fresh = true; }

        /** @brief Draw a label at column 2. Its own glyphs only, no
         *  clear-to-EOL: a label is always drawn onto a freshly-cleared screen
         *  (focus / Ctrl-L), and a clear would wipe an adjacent block. Not
         *  value-cached. */
        void _label(uint16_t row, const char* s) { _label_at(row, 2, s); }


        /** @brief Draw a dash-ruled group header "==== TITLE ====" padded to
         *  @p width. Not cached. */
        void _header(uint16_t row, uint16_t col, const char* title, uint16_t width);

        /** @brief Draw a static label at (@p row, @p col) without clear-to-EOL,
         *  so it never disturbs a second column on the same row. Not cached. */
        void _label_at(uint16_t row, uint16_t col, const char* s);

        /** @brief Render @p s as a fixed @p width-column value cell at
         *  (@p row, @p col), space-padded, with watch(1) -d highlighting:
         *  diffed against what is already ON THE GRID there, per digit run,
         *  and never lit on a repaint (@see _fresh).
         *  @note Decides how to attribute a change, never whether to draw. */
        void _cell(uint16_t row, uint16_t col, const char* s,
                   uint16_t width, Highlight highlight = Highlight::LIT) {
            if (row >= _console.rows() || width == 0) return;
            if (width > FIELD_MAX) width = FIELD_MAX;

            char cf[FIELD_MAX + 1];
            Terminal::Field::render(cf, s, width);

            uint32_t newhi = 0;
            if (!_fresh && highlight == Highlight::LIT) {
                char prev[FIELD_MAX + 1];
                const uint16_t got = _terminal.front_cells(row, col, prev, width);
                for (uint16_t i = got; i < width; i++) prev[i] = ' ';

                // Per-column change mask -> whole-subfield highlight: a digit
                // run lights if any of its digits moved.
                const uint32_t chg = Terminal::Field::changed(cf, prev, width);
                for (uint16_t i = 0; i < width; ) {
                    if (cf[i] < '0' || cf[i] > '9') { ++i; continue; }
                    uint16_t a = i;
                    while (i < width && cf[i] >= '0' && cf[i] <= '9') ++i;
                    uint32_t run =
                        (((i - a) >= 32) ? ~0u : ((1u << (i - a)) - 1u)) << a;
                    if (chg & run) newhi |= run;   // any digit changed -> lit
                }
            }

            _terminal.paint_field_runs(row, col, cf, width, newhi);
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
         *  to _refresh_ms); other keys ignored. */
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
