#ifndef BOTANIK_TEST_PAGE_FIXTURE_H
#define BOTANIK_TEST_PAGE_FIXTURE_H

/**
 * @file page_fixture.h
 * @brief A console with one thing on it, and the two ways to assert about what
 *        that thing drew.
 *
 * The two questions are different and a suite wants both:
 *
 *  - **CONVERGENCE** - does the incremental screen equal the one a full repaint
 *    would produce? The editor and every page reconcile their own rows, so the
 *    screen can drift from the model and the terminal's own diff cannot notice:
 *    FRONT and BACK agree, and the wrong glyph stays. `converged()` is that
 *    check, and it is what four of this round's console findings were.
 *  - **BYTE BUDGET** - what did NOT reach the wire? A page that repaints
 *    everything is still correct, so convergence passes while the link at 4800
 *    baud does not keep up. `paint()` hands back the bytes to assert on.
 *
 * Pages are driven DIRECTLY, not through Console's own beat: the status bar
 * carries a live clock, so a whole-console convergence check would be flaky by
 * construction. What is on screen here is what the page under test put there.
 */

#include <string>
#include <vector>

#include "doctest.h"

#include "libbotanik.h"

#include "term_fixture.h"

namespace botanik_test {

    /** @brief A Console on a pipe, at a stated geometry.
     *
     *  The three geometries worth covering are the ends of what the operator
     *  can actually ask for: `console_cols` is banded 80..132 and
     *  `console_rows` 20..32, so a page correct only at the 80x24 default is a
     *  page nobody has tested. */
    struct PageFixture {
        PipeSerial          wire;
        uint64_t            now = 0;
        Botanik::Terminal::Console console;

        explicit PageFixture(uint16_t rows = 24, uint16_t cols = 80)
            : console(Botanik::SetupContext(), wire.serial, &now) {
            console.reconfigure(Botanik::Terminal::ConsoleConfig().type, cols, rows);
            // Far above any real link: a paced paint would otherwise leave the
            // grid holding what the wire has not carried yet, and every
            // assertion here would be about the pacing rather than the page.
            console.terminal().set_baud(4000000u);
        }

        /** @brief Drop whatever is queued, so the next paint() answers for one
         *  action rather than for the session so far. */
        void drain() { (void)wire.capture(); }

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
                out += wire.capture();
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
                INFO(what << ": row " << r
                     << "\n  incremental: '" << incremental[r] << "'"
                     << "\n  full redraw: '" << full[r] << "'");
                CHECK(incremental[r] == full[r]);
            }
        }

        /** @brief Assert @p page's incremental screen equals the one it would
         *  draw from scratch.
         *
         *  @warning Not valid mid-edit. MonitorPage::focus() and
         *  ParamsPage::focus() clear their `_editing` flag, so a page asked to
         *  repaint while an inline edit is open legitimately comes back
         *  different. Leave the edit first, or assert convergence around it.
         */
        void converged(Botanik::Pages::Page& page, const char* what) {
            const std::vector<std::string> incremental = content();
            console.terminal().establish_screen();
            page.redraw();
            (void)paint(page);
            same(incremental, content(), what);
        }
    };

}  // namespace botanik_test

#endif // BOTANIK_TEST_PAGE_FIXTURE_H
