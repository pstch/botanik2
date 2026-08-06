/**
 * @file test_scrollback.cpp
 * @brief Pages::Scrollback - the scrolled-back view over the line ring.
 *
 * The view is a single number, _view_offset: how many lines back from the
 * newest the window is parked. What is worth pinning is what it MEANS. It
 * counts from the newest line, so a line arriving under a frozen view slides
 * every line the offset names - the window would keep its pixels and lose its
 * place, and the next scroll would paint rows that do not join up with the ones
 * above them.
 *
 * The offset is private, and stays that way: a pipe-backed AnsiTerminal makes
 * the emitted frame the oracle, so each case asserts on the lines a scroll
 * actually painted. Where the offset itself is the point, it is read back off
 * the wire out of the "v N more" tag rather than through an accessor written
 * for the tests.
 */
#include <cstdint>                    // uint16_t - line counts and screen geometry
#include <cstdio>                     // snprintf / sscanf - the line labels and the tag
#include <fcntl.h>                    // fcntl / O_NONBLOCK - the pipe standing in for the link
#include <string>                     // std::string - the emitted byte stream asserted on
#include <unistd.h>                   // pipe / read / close - draining what present() emitted

#include "libbotanik/console/terminal.h" // AnsiTerminal - the device the view paints through
#include "libbotanik/pages/scrollback.h" // Pages::Scrollback - under test
#include "libbotanik/utils/serial.h"  // Utils::BufferedSerial - the wire present() is paced against

#include "doctest.h"                  // the test framework (vendored single header); last so it sees our types

using namespace Botanik;

namespace {

    // The window under test: 5 rows of a 10-row screen, starting at row 1, so
    // there is chrome above and below that a scroll must not touch.
    const uint16_t FIRST_ROW = 1;
    const uint16_t LOG_ROWS  = 5;

    /** @brief A Scrollback over a pipe-backed terminal, drained into a string. */
    struct ScrollFixture {
        int                     fds[2];
        HAL::Serial::Raw        raw;
        Utils::BufferedSerial   serial;
        Terminal::AnsiTerminal  term;
        // 16 lines: enough history behind a 5-row window to scroll through, few
        // enough that a test can walk the ring off its own end.
        Pages::Scrollback<16>   view;

        // Baud far above the link so the byte budget never throttles a frame:
        // these cases assert what a scroll paints, not how it is paced.
        ScrollFixture() : serial(&raw), term(serial, 20, 10, 4000000u) {
            REQUIRE(::pipe(fds) == 0);
            ::fcntl(fds[0], F_SETFL, O_NONBLOCK);   // read end never blocks
            raw.set_fds(fds[0], fds[1]);
            term.establish_screen();                // blank both grids
            capture();                              // discard the bring-up bytes
        }
        ~ScrollFixture() { ::close(fds[0]); ::close(fds[1]); }

        /** @brief Push the TX ring to the pipe and return what accumulated. */
        std::string capture() {
            std::string out;
            char buf[8192];
            for (int i = 0; i < 64; i++) serial.transmit();
            ssize_t n;
            while ((n = ::read(fds[0], buf, sizeof buf)) > 0) out.append(buf, (size_t)n);
            return out;
        }
        /** @brief One frame: reconcile FRONT to the wire and read the bytes. */
        std::string frame() { term.present(); return capture(); }

        /** @brief Blank both grids, so the next frame reports every cell the
         *  operation paints rather than its diff against what was already
         *  there. Repainting "L02" as "L16" otherwise emits two digits, not the
         *  line - which is the terminal doing its job, and is pinned where it
         *  belongs, in test_terminal_present.cpp. */
        void forget_screen() { term.establish_screen(); capture(); }

        /** @brief Bank line "Lnn" as the live tail does, active so the view
         *  tracks it. */
        void feed(uint16_t n) {
            char line[8];
            int len = snprintf(line, sizeof(line), "L%02u", (unsigned)n);
            view.ingest(line, (size_t)len, term.cols(), "", 0, /*active=*/true);
        }
        /** @brief Bank L0..L(n-1) and paint the window from them. */
        void fill(uint16_t n) {
            for (uint16_t i = 0; i < n; i++) feed(i);
            view.begin_redraw(LOG_ROWS);            // window shows the newest
            view.drain_redraw(term);
            frame();                                // and it is on the wire
        }

        /** @brief The offset the "v N more" tag reports, read off the wire; -1
         *  when the frame drew no tag. Only meaningful where the tag is drawn
         *  onto rows that did not already carry one - re-tagging 2 as 4 changes
         *  a single cell, and a single cell is all present() then emits. */
        int tagged_offset() {
            view.refresh_scroll_indicator(term, FIRST_ROW, LOG_ROWS);
            std::string out = frame();
            size_t at = out.find(" v ");
            if (at == std::string::npos) return -1;
            int n = -1;
            if (sscanf(out.c_str() + at, " v %d more", &n) != 1) return -1;
            return n;
        }

        bool scroll_up(uint16_t n) { return view.scroll(term, true, n, FIRST_ROW, LOG_ROWS); }
    };

    /** @brief True if @p hay contains @p needle. */
    bool has(const std::string& hay, const char* needle) {
        return hay.find(needle) != std::string::npos;
    }

}

// The offset is anchored to the newest line, so a scroll of n exposes the n
// lines just older than the window's top - the baseline the drift case below
// is a variation on.
TEST_CASE("scrollback: scrolling up exposes the lines above the window") {
    ScrollFixture f;
    f.fill(12);                       // L00..L11; window shows L07..L11
    CHECK(f.tagged_offset() == -1);   // at the live bottom: no tag

    CHECK(f.scroll_up(2));
    std::string out = f.frame();
    CHECK(has(out, "L05"));           // the two rows the scroll uncovered
    CHECK(has(out, "L06"));
    CHECK(f.tagged_offset() == 2);
}

// The drift. A line banked under a frozen view shifts every line the offset
// names one step further from the newest; left alone, the window would keep
// showing L05..L09 while the offset now said L06..L10, and the next scroll
// would paint rows that did not join the ones above them. Advancing the offset
// by as much keeps it naming what is actually on screen.
TEST_CASE("scrollback: a frozen view keeps its place as lines arrive under it") {
    ScrollFixture f;
    f.fill(12);
    CHECK(f.scroll_up(2));            // window on L05..L09, two newer below
    f.frame();

    f.feed(12);                       // two more arrive while scrolled back
    f.feed(13);
    CHECK(f.view.drain_tail(f.term, FIRST_ROW, LOG_ROWS) == false);  // frozen
    CHECK(f.frame().empty());         // ...so the window does not move

    // The next scroll therefore continues from L05 - the line actually at the
    // top of the window. Left at its old value the offset would name L07 by
    // now, and this would paint L05/L06 again, one row above themselves.
    CHECK(f.scroll_up(2));
    std::string out = f.frame();
    CHECK(has(out, "L03"));
    CHECK(has(out, "L04"));
    CHECK_FALSE(has(out, "L06"));
}

// A view can be read out from under: the ring keeps evicting while the offset
// keeps counting, so the lines it named stop existing. It re-anchors on the
// oldest line there is and repaints - the alternative is leaving evicted text
// on screen and painting the next scroll's rows above it.
TEST_CASE("scrollback: a view left behind by the ring re-anchors on the oldest line") {
    ScrollFixture f;
    f.fill(12);
    CHECK(f.scroll_up(5));
    f.frame();

    // Twenty more: the 16-line ring has wrapped past everything the view was
    // parked on, and the offset it walked to is far beyond what is left.
    for (uint16_t i = 12; i < 32; i++) f.feed(i);

    // The key is absorbed by the re-anchor (there is nothing further up), but
    // the window still changed, so it reports true and repaints whole.
    f.forget_screen();                // report painted cells, not a diff
    CHECK(f.scroll_up(3));
    std::string out = f.frame();
    CHECK(has(out, "L16"));           // the oldest of the 16 still stored...
    CHECK(has(out, "L20"));           // ...through the bottom of the window
}

// The whole point of the freeze is that it ends: scroll_to_bottom() is what
// every key but PAGE_UP/PAGE_DOWN calls on the shell page.
TEST_CASE("scrollback: scroll_to_bottom releases the frozen tail") {
    ScrollFixture f;
    f.fill(12);
    CHECK(f.scroll_up(4));
    f.frame();
    CHECK(f.tagged_offset() == 4);

    f.feed(12);
    CHECK(f.view.drain_tail(f.term, FIRST_ROW, LOG_ROWS) == false);  // frozen

    CHECK(f.view.scroll_to_bottom(f.term, FIRST_ROW, LOG_ROWS));
    std::string out = f.frame();
    CHECK(has(out, "L12"));           // the newest line is back in the window
    CHECK(f.tagged_offset() == -1);   // ...and the tag is gone

    // The tail runs again from here.
    f.feed(13);
    CHECK(f.view.drain_tail(f.term, FIRST_ROW, LOG_ROWS));
}

// At the bottom already, so nothing to do and nothing to pay: the shell calls
// this on every keystroke.
TEST_CASE("scrollback: scroll_to_bottom is free at the live bottom") {
    ScrollFixture f;
    f.fill(12);
    CHECK(f.view.scroll_to_bottom(f.term, FIRST_ROW, LOG_ROWS) == false);
    CHECK(f.frame().empty());
}

// A buffer that fits the window has nothing to scroll, and must not leave a
// stale offset holding the tail frozen forever.
TEST_CASE("scrollback: a buffer that fits the window cannot be scrolled") {
    ScrollFixture f;
    f.fill(3);                        // fewer lines than the 5-row window
    CHECK(f.scroll_up(2) == false);
    CHECK(f.tagged_offset() == -1);
    f.feed(3);
    CHECK(f.view.drain_tail(f.term, FIRST_ROW, LOG_ROWS));  // tail still live
}
