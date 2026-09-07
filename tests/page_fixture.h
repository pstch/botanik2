#ifndef BOTANIK_TEST_PAGE_FIXTURE_H
#define BOTANIK_TEST_PAGE_FIXTURE_H

/**
 * @file page_fixture.h
 * @brief A console with one thing on it, and the two ways to assert about what
 *        that thing drew.
 *
 * The two questions are different and a suite wants both:
 *
 *  - Convergence: does the incremental screen equal the one a full repaint
 *    would produce? The editor and every page reconcile their own rows, so the
 *    screen can drift from the model without the terminal's own diff noticing -
 *    FRONT and BACK agree, and the wrong glyph stays. `converged()` asks.
 *  - Byte budget: what did not reach the wire? A page that repaints everything
 *    is still correct, so convergence passes while the link at 4800 baud does
 *    not keep up. `paint()` hands back the bytes to assert on.
 *
 * Pages are driven directly rather than through Console's own beat: the status
 * bar carries a live clock, which would make a whole-console convergence check
 * flaky by construction.
 */

#include <string>
#include <vector>

#include "doctest.h"

#include "libbotanik.h"

#include "term_fixture.h"
#include "vt_model.h"

namespace botanik_test {

    /** @brief A Console on a pipe, at a stated geometry.
     *
     *  `console_cols` is banded 80..132 and `console_rows` 20..32, so the
     *  geometries worth covering are the ends of that plus the 80x24 default. */
    struct PageFixture {
        PipeSerial          wire;
        uint64_t            now = 0;
        Botanik::Terminal::Console console;
        /** @brief An independent VT fed every byte that left the ring. FRONT
         *  says what the renderer meant, this what the operator sees. */
        VtScreen            vt;

        explicit PageFixture(uint16_t rows = 24, uint16_t cols = 80)
            : console(Botanik::SetupContext(), wire.serial, &now) {
            console.reconfigure(Botanik::Terminal::ConsoleConfig().type, cols, rows);
            // Far above any real link. A paced paint would leave the grid
            // holding what the wire has not carried yet, and every assertion
            // here would be about the pacing rather than the page.
            console.terminal().set_baud(4000000u);
            vt.reshape(console.terminal().rows_total(), console.terminal().cols());
        }

        /** @brief Drop whatever is queued, so the next paint() answers for one
         *  action rather than for the session so far. The VT still sees it:
         *  a model that skipped bytes would diverge from the wire, not from
         *  the terminal. */
        void drain() { vt.feed(wire.capture()); vt.reset_counter(); }

        /** @brief Reconcile until the ring stops producing, and hand back the
         *  bytes. What a renderer that is not a Page (the editor) drives. */
        std::string pump() {
            std::string out;
            for (int i = 0; i < 64; i++) {
                console.terminal().present();
                const std::string got = wire.capture();
                if (got.empty() && i > 0) break;
                out += got;
                vt.feed(got);
            }
            return out;
        }

        /** @brief Blank the screen, the grids and the VT together, so what
         *  follows is measured from nothing.
         *  @note Twice: a screen's first establish stamps a 4-byte boot banner
         *  the grids do not carry, and the second one's ED is what wipes it. */
        void reset_wire() {
            console.terminal().establish_screen();
            console.terminal().establish_screen();
            (void)pump();
        }

        /** @brief The first cell where the VT and FRONT disagree, described;
         *  empty when the operator's screen is the one FRONT asked for.
         *  One string rather than a CHECK per cell: this runs per keystroke in
         *  the fuzz, where 1920 assertions a step is unusable. */
        std::string wire_diff() const {
            const Botanik::Terminal::AnsiTerminal& t = console.terminal();
            char msg[256];
            for (uint16_t r = 0; r < t.rows_total(); r++)
                for (uint16_t c = 0; c < t.cols(); c++) {
                    char       want[2] = { 0, 0 };
                    const bool want_inv = t.front_inverted(r, c);
                    t.front_cells(r, c, want, 1);
                    if (want[0] == vt.glyph[r][c] && want_inv == vt.inv[r][c]) continue;
                    snprintf(msg, sizeof msg,
                             "row %u col %u: FRONT '%c'%s, wire '%c'%s"
                             "\n  FRONT: '%s'\n  wire : '%s'",
                             (unsigned)r, (unsigned)c,
                             want[0], want_inv ? " (inv)" : "",
                             vt.glyph[r][c], vt.inv[r][c] ? " (inv)" : "",
                             _front_row_text(r).c_str(), vt.row_text(r).c_str());
                    return std::string(msg);
                }
            return std::string();
        }

        /** @brief Assert the VT's screen is the one FRONT asked for, cell for
         *  cell, reverse video included. The check that survives moving
         *  reconciliation OUT of a renderer: FRONT then becomes correct by
         *  construction and only the wire can still be wrong. */
        void check_wire(const char* what) {
            const std::string diff = wire_diff();
            INFO(std::string(what) << ": the wire does not show what FRONT drew\n  "
                 << diff);
            CHECK(diff.empty());
        }

        /** @brief FRONT row @p r as text, right-trimmed. */
        std::string _front_row_text(uint16_t r) const {
            char buf[VtScreen::MAX_COLS + 1];
            const uint16_t n = console.terminal().front_row(r, buf, sizeof buf);
            std::string s(buf, n);
            while (!s.empty() && s.back() == ' ') s.pop_back();
            return s;
        }

        /** @brief FRONT rows [@p first, @p last), one string each, right-trimmed. */
        std::vector<std::string> rows(uint16_t first, uint16_t last) {
            std::vector<std::string> out;
            char row[160];
            const uint16_t n_rows = console.terminal().rows_total();
            if (last > n_rows) last = n_rows;
            for (uint16_t r = first; r < last; r++) {
                const uint16_t n = console.terminal().front_row(r, row, sizeof row);
                std::string s(row, n);
                while (!s.empty() && s.back() == ' ') s.pop_back();
                out.push_back(s);
            }
            return out;
        }

        /** @brief The whole FRONT grid, chrome included. For a failure message
         *  or a test that means to assert about the bars. */
        std::vector<std::string> screen() {
            return rows(0, console.terminal().rows_total());
        }

        /** @brief Just the band a PAGE owns: rows 1 .. rows()-1.
         *
         *  Row 0 is the status bar and the reserved bottom rows are the alarm
         *  bar, both painted by Console and neither redrawn by Page::redraw().
         *  Comparing them would fail on the status bar's live clock rather than
         *  on anything the page did. */
        std::vector<std::string> content() {
            return rows(1, console.rows());
        }

        /** @brief Step @p page until its paint settles, and hand back the bytes
         *  that reached the wire. */
        std::string paint(Botanik::Pages::Page& page) {
            std::string out;
            for (int i = 0; i < 64; i++) {
                page.tick(true);
                console.terminal().present();
                const std::string got = wire.capture();
                out += got;
                vt.feed(got);
            }
            return out;
        }

        /** @brief Assert two screens match, naming the first row that differs.
         *  Split out so a caller that repaints something other than a Page -
         *  the editor draws its own band - can use the same comparison. */
        static void same(const std::vector<std::string>& incremental,
                         const std::vector<std::string>& full,
                         const char* what) {
            REQUIRE(incremental.size() == full.size());
            for (size_t r = 0; r < full.size(); r++) {
                INFO(std::string(what) << ": row " << r
                     << "\n  incremental: '" << incremental[r] << "'"
                     << "\n  full redraw: '" << full[r] << "'");
                CHECK(incremental[r] == full[r]);
            }
        }

        /** @brief Assert @p page's incremental screen equals the one it would
         *  draw from scratch.
         *
         *  @warning Only where a repaint is meant to reproduce the screen.
         *  Two documented cases where it is not: MonitorPage::focus() and
         *  ParamsPage::focus() clear `_editing`, and Scrollback::begin_redraw()
         *  resets `_view_offset`. So a page repainted mid-edit, or scrolled
         *  back, legitimately comes back different. Leave the edit or return to
         *  the bottom first, and assert convergence around it.
         */
        void converged(Botanik::Pages::Page& page, const char* what) {
            check_wire(what);
            const std::vector<std::string> incremental = content();
            // A page that drew nothing converges trivially, so the comparison
            // below would pass on a page that had stopped painting entirely.
            size_t drawn = 0;
            for (const std::string& row : incremental) if (!row.empty()) drawn++;
            INFO(std::string(what) << ": the page painted nothing at all");
            CHECK(drawn > 0);
            console.terminal().establish_screen();
            page.redraw();
            (void)paint(page);
            same(incremental, content(), what);
        }
    };

}  // namespace botanik_test

#endif // BOTANIK_TEST_PAGE_FIXTURE_H
