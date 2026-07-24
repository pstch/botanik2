/**
 * @file test_terminal_present.cpp
 * @brief AnsiTerminal::present() - the back-buffer diff, directly.
 *
 * present() is where the whole restricted-TX-budget story lives: it diffs the
 * FRONT grid renderers drew against BACK (the wire), emits the minimal ANSI to
 * reconcile them, and paces that under a per-tick byte budget without ever
 * emitting a partial run. A pipe-backed AnsiTerminal lets us drain TX and assert
 * exactly what a frame wrote, so the byte stream is the oracle.
 *
 * The four cases pin the invariants the design rests on: minimal emit, pacing
 * across ticks, scroll-then-diff (a DL/IL beats a band repaint), and the
 * all-or-nothing ring gate.
 */
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>

#include "doctest.h"

#include "libbotanik/console/terminal.h"
#include "libbotanik/utils/serial.h"

using namespace Botanik;

namespace {
    /** @brief A bare AnsiTerminal over a pipe, drained into a string. */
    struct TermFixture {
        int                    fds[2];
        HAL::Serial::Raw       raw;
        Utils::BufferedSerial  serial;
        Terminal::AnsiTerminal term;

        // Small screen so a row is cheap to assert. The default baud is far above
        // the link so the budget never throttles; a test that wants pacing passes
        // a low one (budget = baud * TX_COLD_TARGET_MS / 10000).
        explicit TermFixture(uint16_t cols = 20, uint16_t rows = 10,
                             uint32_t baud = 4000000u)
            : serial(&raw), term(serial, cols, rows, baud) {
            REQUIRE(::pipe(fds) == 0);
            ::fcntl(fds[0], F_SETFL, O_NONBLOCK);   // read end never blocks
            raw.set_fds(fds[0], fds[1]);
            term.establish_screen();                // blank both grids
            capture();                              // discard the bring-up bytes
        }
        ~TermFixture() { ::close(fds[0]); ::close(fds[1]); }

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
        std::string present() { term.present(); return capture(); }
        /** @brief Present until the screen settles, returning everything emitted. */
        std::string settle(int ticks = 32) {
            std::string out;
            for (int i = 0; i < ticks; i++) out += present();
            return out;
        }
        void put(uint16_t row, uint16_t col, const char* s) {
            term.move_to({row, col});
            term.write_chars(s, (uint16_t)strlen(s));
        }
    };
}

// The diff's core contract: one changed cell costs a move plus that glyph, and a
// frame with nothing new costs nothing at all.
TEST_CASE("present: one changed cell emits only that cell") {
    TermFixture f;
    f.put(2, 5, "X");
    std::string out = f.present();

    CHECK(out.find('X') != std::string::npos);
    // A move to the cell, the glyph, and the idle-cursor park - nothing like a
    // row, let alone a screen.
    CHECK(out.size() < 24);
    CHECK(f.present().empty());        // settled: a second frame writes nothing
}

// A frame whose changes exceed the tick's byte budget emits what fits and leaves
// the rest in FRONT; later frames drain it. Two separated runs so the first fits
// and the second is deferred (a single run larger than the whole budget takes the
// oversized escape and goes out whole, by design).
TEST_CASE("present: changes beyond the tick budget are paced across frames") {
    TermFixture f(20, 10, 1200u);      // budget = 1200 * 128 / 10000 = 15 bytes
    f.put(1, 0, "AAAA");
    f.put(5, 0, "BBBB");

    std::string first = f.present();
    CHECK(first.find('A') != std::string::npos);   // the first run fit
    CHECK(first.find('B') == std::string::npos);   // the second was deferred

    std::string rest = f.settle();
    CHECK(rest.find('B') != std::string::npos);    // and arrives on a later frame
    CHECK(f.present().empty());                    // converged
}

// A band scroll rides a DL/IL: BACK is shifted to mirror the wire, so the diff
// then repaints only the revealed row - not the whole band.
TEST_CASE("present: a band scroll emits a DL/IL, not a band repaint") {
    TermFixture f(20, 10);
    for (uint16_t r = 1; r <= 6; r++) {
        char line[8]; snprintf(line, sizeof line, "row%u", (unsigned)r);
        f.put(r, 0, line);
    }
    f.settle();                        // screen established and quiet

    // Scroll the band up one and redraw the shifted content: rows 1..5 now hold
    // what 2..6 held, and row 6 is revealed as "row7".
    f.term.scroll_region(1, 6, /*up=*/true, 1);
    for (uint16_t r = 1; r <= 6; r++) {
        char line[8]; snprintf(line, sizeof line, "row%u", (unsigned)(r + 1));
        f.put(r, 0, line);
    }
    std::string out = f.settle();

    CHECK(out.find('M')     != std::string::npos);  // DL (ESC[1M) - the scroll
    CHECK(out.find("row7")  != std::string::npos);  // the revealed row painted
    CHECK(out.find("row3")  == std::string::npos);  // a kept row did NOT repaint
    CHECK(out.size() < 64);                         // nowhere near a band repaint
}

// The ring is droppable, so a run is emitted only when it fits whole: with the
// ring stuffed, present() writes nothing rather than a torn half-row, and the
// change survives in FRONT to be emitted once there is room.
TEST_CASE("present: a run that cannot fit the ring is deferred whole") {
    TermFixture f;
    // Stuff the TX ring so no run can fit ("HELLO" costs ~8 + 5 bytes).
    const char pad = '.';
    while (f.serial.availableForWrite() > 12) f.serial.write(&pad, 1);

    f.put(2, 0, "HELLO");
    f.term.present();                  // must not emit a partial run
    f.capture();                       // drain the stuffing (and anything else)

    std::string out = f.settle();      // now there is room
    CHECK(out.find("HELLO") != std::string::npos);
    CHECK(f.present().empty());        // converged
}

// With no renderer asking for a visible cursor, a settled frame leaves the real
// cursor idling in the bottom-right corner rather than wherever the last run
// ended - so it never sits in the middle of the content.
TEST_CASE("present: the idle cursor parks in the bottom-right corner") {
    TermFixture f(20, 10);
    f.put(3, 4, "mid-screen");
    f.settle();
    // Draw nothing new; the cursor is already parked, so a frame is free...
    CHECK(f.present().empty());
    // ...and a fresh change re-parks it there afterwards: the last thing emitted
    // is a move, not a glyph.
    f.put(3, 4, "MID-SCREEN");
    std::string out = f.settle();
    // The '-' is unchanged, so the diff emits two runs around it rather than the
    // whole string - which is the point.
    CHECK(out.find("MID")    != std::string::npos);
    CHECK(out.find("SCREEN") != std::string::npos);
    CHECK(out.find("MID-SCREEN") == std::string::npos);
    CHECK(out.back() == 'H');          // ends on an absolute CUP (ESC[r;cH)
}

// A row shifted sideways is the case a plain cell diff is worst at: every column
// from the shift point on differs, so a naive diff rewrites the whole tail.
// _shift_row spots the slide and sends one escape (SM4/RM4 insert mode, or DCH)
// instead - but only when that actually beats rewriting the span.
TEST_CASE("present: a row shifted right rides an insert, not a tail rewrite") {
    TermFixture f(80, 10);
    const char* text = "the quick brown fox jumps over the lazy dog and keeps running";
    f.put(3, 0, text);
    f.settle();

    // Same text, one column over: cells 1.. all differ from BACK.
    f.put(3, 0, "X");
    f.put(3, 1, text);
    std::string out = f.settle();

    CHECK(out.find('X') != std::string::npos);
    CHECK(out.find("quick brown fox") == std::string::npos);  // tail not rewritten
    CHECK(out.size() < 24);
}

// The mirror case: deleting a column shifts the tail left, which is a DCH.
TEST_CASE("present: a row shifted left rides a DCH") {
    TermFixture f(80, 10);
    const char* text = "the quick brown fox jumps over the lazy dog and keeps running";
    f.put(3, 0, "X");
    f.put(3, 1, text);
    f.settle();

    f.put(3, 0, text);
    f.term.move_to({3, (uint16_t)strlen(text)});
    f.term.clear_to_eol();
    std::string out = f.settle();

    CHECK(out.find("quick brown fox") == std::string::npos);  // tail not rewritten
    CHECK(out.size() < 24);
}
