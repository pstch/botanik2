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
 * The cases pin the invariants the design rests on: minimal emit, pacing across
 * ticks, scroll-then-diff (a DL/IL beats a band repaint), the all-or-nothing ring
 * gate, the priority classes, and the top-down row order within a tick.
 */
#include <cstdint>                          // uint8_t/uint16_t
#include <cstdio>                           // snprintf
#include <cstring>                          // strlen / memcmp
#include <fcntl.h>                          // fcntl / O_NONBLOCK
#include <string>                           // std::string
#include <unistd.h>                         // pipe / read / close

#include "libbotanik/console/terminal.h"    // AnsiTerminal::present
#include "libbotanik/utils/serial.h"        // Utils::BufferedSerial

#include "doctest.h"

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

// The control for the two ordering tests below: with every row in one class the
// walk really is top-down, so row 0 wins the budget. Without this they could pass
// for the wrong reason.
TEST_CASE("present: without priorities the top row wins the budget") {
    TermFixture f(20, 10, 1200u);
    f.settle();

    f.put(0, 0, "12:34:56");
    f.put(4, 0, "PAGECONTENT");
    std::string first = f.present();

    CHECK(first.find("12:34:56")   != std::string::npos);
    CHECK(first.find("PAGECONTENT") == std::string::npos);
}

// The alarm strip is the physical bottom row, so top-down order alone puts it
// behind every page row above it - the frame arriving after the thing it frames,
// which reads as a fault on a slow link. PRIO_CHROME outranks page structure to
// stop that, and this pins it: a bottom row drawn as chrome beats a page row well
// above it.
TEST_CASE("present: the bottom chrome row is drawn before the page it frames") {
    TermFixture f(20, 10, 1200u);        // budget = 15 bytes a tick
    f.settle();

    uint16_t bottom = 9;
    f.term.set_row_priority(bottom, bottom, Terminal::PRIO_CHROME);
    f.put(2, 0, "PAGEROW");              // page content, nearer the top
    f.put(bottom, 0, "ALARMS:");         // the frame, at the very bottom
    std::string first = f.present();

    CHECK(first.find("ALARMS:")  != std::string::npos);
    CHECK(first.find("PAGEROW") == std::string::npos);
    CHECK(f.settle().find("PAGEROW") != std::string::npos);   // page follows
}

// A run is emitted either because it fits this tick's remaining budget, or
// because it is too big to ever fit one and takes the oversized escape. Those two
// tests have to overlap, or a run in the gap between them is deferred every tick
// forever. They did not: tx_used() reports a byte in use even on an empty ring,
// so the budget is always under the full depth, and a run costing exactly the
// full depth fell straight through the crack.
TEST_CASE("present: a run costing exactly the tick budget still goes out") {
    TermFixture f(20, 10, 1200u);        // depth = 1200 * 128 / 10000 = 15 bytes
    f.settle();
    // A leading absolute move is budgeted at 8, so 7 glyphs costs exactly 15.
    f.put(3, 0, "SEVENCH");
    std::string out = f.settle();
    CHECK(out.find("SEVENCH") != std::string::npos);
    CHECK(f.present().empty());          // and it converged rather than looping
}

// The STATUS page is a screenful of live fields all updating on the same cadence.
// They share one priority class, so priority alone does nothing for them - and
// walking the class top-down means the upper rows spend the whole budget every
// tick and the lower ones never reach the wire at all. That is starvation within
// a class, and it is what a FIFO queue would have fixed by fairness.
TEST_CASE("present: no row starves when every row updates every tick") {
    TermFixture f(20, 12, 1200u);        // 15-byte budget, ~half a row
    f.settle();

    bool seen[9] = { false };
    for (int tick = 1; tick <= 300; tick++) {
        for (uint16_t r = 1; r <= 8; r++) {   // every row dirties every tick
            char line[24];
            snprintf(line, sizeof line, "R%u=%03d", (unsigned)r, tick % 1000);
            f.put(r, 0, line);
        }
        std::string out = f.present();
        for (uint16_t r = 1; r <= 8; r++) {
            char tag[8];
            snprintf(tag, sizeof tag, "R%u=", (unsigned)r);
            if (out.find(tag) != std::string::npos) seen[r] = true;
        }
    }
    for (uint16_t r = 1; r <= 8; r++) {
        INFO("row " << r << " never reached the wire in 300 ticks");
        CHECK(seen[r]);
    }
}

namespace {
    // A row wide enough that two of them cannot share a tick at BAUD_2ROWS, so a
    // frame reliably runs out of budget partway down and parks the walk there.
    const char WIDE[] = "####################";
    // budget = baud * TX_COLD_TARGET_MS / 10000 = 40 bytes, one WIDE row and a bit
    constexpr uint32_t BAUD_2ROWS = 3125u;

    /** @brief Leave the walk parked at @p row with everything above it settled. */
    void strand_walk_at(TermFixture& f, uint16_t row) {
        f.settle();
        f.put((uint16_t)(row - 1), 0, WIDE);   // fits the tick...
        f.put(row, 0, WIDE);                   // ...and this one does not
        f.present();
    }
}

// Whatever a single frame puts on the wire has to read top-down: lines of shell
// output, the rows of a page. An earlier cut resumed each class where the budget
// ran out and then *wrapped* within the tick, so one frame could carry rows
// N..max followed by rows 0..N-1 - the page painting its bottom half first.
TEST_CASE("present: one frame never emits a row before one above it") {
    TermFixture f(20, 12, BAUD_2ROWS);
    strand_walk_at(f, 11);               // budget died on the last row

    f.put(1, 0, "TOP");                  // now dirty a row above the parked walk
    std::string one = f.present();       // ...and take a single frame

    size_t top = one.find("TOP"), bottom = one.find(WIDE);
    INFO("a frame carried row 11 before row 1");
    CHECK((top == std::string::npos || bottom == std::string::npos || top < bottom));
}

// The parked walk is per class and outlives the frame that parked it, so on its
// own it also outlives the page that drew it: the outgoing page's budget ran out
// at row N and the incoming page starts painting there. restart_scan() is what
// the Console calls on a switch - and what a band scroll and a full repaint call
// for themselves.
TEST_CASE("present: restart_scan sends the walk back to the top") {
    TermFixture f(20, 12, BAUD_2ROWS);
    strand_walk_at(f, 11);

    f.term.restart_scan();
    f.put(1, 0, "TOP");
    std::string one = f.present();

    CHECK(one.find("TOP") != std::string::npos);   // the top row leads again
}

// A band scroll is the shell/logs tail: DL the rows off the top, paint the freed
// slots at the bottom oldest-first. Those slots are consecutive lines of output,
// so the wire has to carry them in that order - which it does only because
// scroll_region() restarts the walk, rather than resuming it below them.
TEST_CASE("present: rows revealed by a band scroll go out oldest first") {
    TermFixture f(20, 12, BAUD_2ROWS);
    strand_walk_at(f, 9);                // parked on the band's bottom row

    f.term.scroll_region(1, 9, /*up=*/true, 2);
    f.put(8, 0, "OLDER");                // the two freed slots, in reading order
    f.put(9, 0, "NEWER");
    std::string out = f.settle();

    size_t older = out.find("OLDER"), newer = out.find("NEWER");
    REQUIRE(older != std::string::npos);
    REQUIRE(newer != std::string::npos);
    CHECK(older < newer);
}

// Chrome is not one contiguous band: the status bar is row 0, the alarms legend
// sits mid-screen and the alarm strip is the bottom row. So the class routinely
// parks the walk below one of its own rows - and reaching the bottom from there
// does not mean the class is done. Letting the page through at that point is a
// priority inversion: the alarms table painted while the legend above it waited a
// tick, which is exactly the frame-after-what-it-frames case PRIO_CHROME exists
// to prevent.
TEST_CASE("present: chrome above the parked walk still beats the page") {
    TermFixture f(20, 12, BAUD_2ROWS);
    f.term.set_row_priority(1, Terminal::PRIO_CHROME);    // legend, mid-screen
    f.term.set_row_priority(11, Terminal::PRIO_CHROME);   // alarm strip, bottom
    f.settle();

    // Park the walk on chrome's own bottom row: the legend takes the budget and
    // the strip below it does not fit.
    f.put(1, 0, WIDE);
    f.put(11, 0, WIDE);
    f.present();

    f.put(1, 0, "LEGEND");               // the frame, above the parked walk
    f.put(5, 0, "PG");                   // and the page it frames
    std::string out = f.settle();

    size_t legend = out.find("LEGEND"), page = out.find("PG");
    REQUIRE(legend != std::string::npos);
    REQUIRE(page != std::string::npos);
    INFO("the page was painted before the chrome that frames it");
    CHECK(legend < page);
}

// A row's class is a standing property of what lives there, not something raised
// around a draw. That only works if ownership is handed over cleanly: the Console
// returns the content band to plain structure on a page switch and the incoming
// page declares its own rows in focus(). Without that handover the outgoing
// page's classes (the shell editor's rows, say) would outrank the new page's
// content for good - which is how the first cut of this leaked.
TEST_CASE("present: reclassifying a row hands it back to the page") {
    TermFixture f(20, 10, 1200u);        // budget ~ one row
    f.settle();

    // A page owns row 6 and marks it as carrying keystroke echo.
    f.term.set_row_priority(6, Terminal::PRIO_INPUT);
    f.put(6, 0, "EDITROW");
    f.put(2, 0, "PAGEROW");
    std::string out = f.present();
    CHECK(out.find("EDITROW") < out.find("PAGEROW"));   // echo goes first
    f.settle();

    // Page switch: the band goes back to plain structure, and the row stops
    // outranking its neighbours.
    f.term.set_row_priority(1, 9, Terminal::PRIO_STRUCT);
    f.put(6, 0, "was-edit");
    f.put(2, 0, "now-page");
    out = f.present();
    CHECK(out.find("now-page") != std::string::npos);   // top-down again
    CHECK(out.find("was-edit") == std::string::npos);
}

// A scroll only mirrors into BACK if the whole DL/IL actually reached the wire.
// The ring drops silently, so shifting BACK after a dropped escape is the worst
// possible outcome: the diff would then believe those rows were already correct
// and never repaint them, leaving the band wrong until a full clear.
TEST_CASE("present: a scroll whose escape does not fit leaves BACK alone") {
    TermFixture f(20, 10);
    for (uint16_t r = 1; r <= 6; r++) {
        char line[8]; snprintf(line, sizeof line, "row%u", (unsigned)r);
        f.put(r, 0, line);
    }
    f.settle();

    // Stuff the ring so neither the DL nor its compensating IL can be emitted.
    const char pad = '.';
    while (f.serial.availableForWrite() > 4) f.serial.write(&pad, 1);

    f.term.scroll_region(1, 6, /*up=*/true, 1);
    for (uint16_t r = 1; r <= 6; r++) {
        char line[8]; snprintf(line, sizeof line, "row%u", (unsigned)(r + 1));
        f.put(r, 0, line);
    }
    f.term.present();          // the scroll cannot go out
    f.capture();               // drain the stuffing

    // The band must still converge. With BACK left unshifted the diff repaints
    // whatever really differs - here just the digit per row, since row1 -> row2
    // is a one-cell change - and the screen ends consistent. Convergence is the
    // property under test: a wrongly-mirrored BACK would leave FRONT and the wire
    // permanently disagreeing, and present() would go quiet with the band wrong.
    std::string out = f.settle();
    CHECK(out.size() > 0);              // the band was reconciled somehow
    CHECK(f.present().empty());         // and FRONT now matches BACK
}
