/**
 * @file test_console.cpp
 * @brief The console: terminal diff/present, the editor, input parsing, scrollback, shell.
 *
 * Each section below is the former test_<name>.cpp, kept in its own
 * namespace so their fixtures and helpers cannot collide.
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
#include "libbotanik/console/console.h" // Console
#include "libbotanik/console/editor.h"  // Terminal::TextEditor
#include "libbotanik/console/parser.h"  // Terminal::InputParser
#include "libbotanik/pages/line_buffer.h"   // Pages::LineBuffer
#include "libbotanik/pages/scrollback.h"    // Pages::Scrollback
#include "libbotanik/shell/shell.h" // Shell::CommandShell
#include "libbotanik/vars/declare.h"

// --------------------------------------------------------------------------
// terminal_present
// --------------------------------------------------------------------------

namespace terminal_present {

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

}  // namespace terminal_present

// --------------------------------------------------------------------------
// editor_bytes
// --------------------------------------------------------------------------

namespace editor_bytes {

using namespace Botanik;

namespace {

    // A TextEditor over a real Console whose serial is a pipe we can drain. Baud
    // is high so present()'s pacing never truncates a frame: we want the total
    // cost of an edit, not what fits in one tick.
    struct EditorBytes {
        int                   fds[2];
        HAL::Serial::Raw      raw;
        Utils::BufferedSerial serial;
        uint64_t              now = 0;
        Terminal::Console     console;
        Terminal::EditorBuffers<Config::SHELL_INPUT_LEN,
                                Config::EDITOR_MAX_ROWS> bufs;
        Terminal::TextEditor  editor;

        explicit EditorBytes(uint32_t baud = 4000000u)
            : serial(&raw), console(serial, 80, 24, &now, baud),
              editor(&console, bufs.storage()) {
            REQUIRE(::pipe(fds) == 0);
            ::fcntl(fds[0], F_SETFL, O_NONBLOCK);
            raw.set_fds(fds[0], fds[1]);
            console.terminal().establish_screen();
            editor.reset();
            flush();                 // discard bring-up + the initial prompt
        }

        /** @brief Re-pin as a top-anchored viewport `rows` tall at row 1, with
         *  the NOTES gutter ("> " on a row the operator started, "  " on its
         *  wrapped tail). Call before typing anything. */
        void as_viewport(uint16_t rows) {
            editor.set_anchor(Terminal::Anchor::TOP);
            editor.set_ps1("> ", 2);
            editor.set_ps2("  ", 2);
            editor.set_ps3("> ", 2);
            editor.set_origin(1);
            editor.set_view_rows(rows);
            editor.reset();
            flush();
        }

        /** @brief Type `n` hard-broken rows "lineNN", flushing as we go.
         *
         *  Two digits by construction - every caller asks for a screenful, not a
         *  hundred rows. Bounded here rather than trusted, so the name cannot
         *  overrun row[] AND the compiler can see that it cannot: unbounded, the
         *  format is an 11-digit int into 8 bytes (-Wformat-truncation). */
        void fill_rows(int n) {
            REQUIRE(n <= 100);
            for (int i = 0; i < n; i++) {
                if (i) editor.insert_characters("\n", 1);
                char row[8];
                snprintf(row, sizeof(row), "line%02u", (unsigned)i % 100u);
                type(row);
            }
            flush();
        }
        ~EditorBytes() { ::close(fds[0]); ::close(fds[1]); }

        /** @brief Present until settled and return everything that reached the pipe. */
        std::string flush() {
            std::string out;
            char buf[8192];
            for (int i = 0; i < 40; i++) {
                console.terminal().present();
                for (int j = 0; j < 8; j++) serial.transmit();
                ssize_t n;
                while ((n = ::read(fds[0], buf, sizeof buf)) > 0) out.append(buf, (size_t)n);
            }
            return out;
        }
        void type(const char* s) {
            editor.insert_characters(s, (uint16_t)strlen(s));
        }
        /** @brief Bytes the next edit costs on the wire. */
        size_t cost_of_type(const char* s) { type(s); return flush().size(); }
    };

}

// Typing at the end of a short line: one glyph per keystroke, plus whatever the
// cursor park costs. This is the common case and it must stay tiny.
TEST_CASE("editor bytes: appending a char costs about a char") {
    EditorBytes f;
    f.type("hello");
    f.flush();

    size_t cost = f.cost_of_type("!");
    CHECK(cost > 0);
    CHECK(cost <= 20);
}

// Inserting at the *start* of a line shifts everything right. Every column past
// the cursor differs from the back-buffer, so a plain cell diff would rewrite the
// whole tail; the terminal recognises the slide and sends one insert instead.
TEST_CASE("editor bytes: inserting at the line head repaints one row, not the screen") {
    EditorBytes f;
    f.type("the quick brown fox jumps over the lazy dog");
    f.flush();

    for (int i = 0; i < 42; i++) f.editor.move_left();   // home, without redrawing
    f.flush();

    size_t cost = f.cost_of_type("X");
    CHECK(cost > 0);
    CHECK(cost <= 28);          // a DCH/insert-mode shift, not a row rewrite
}

// A head insert that also grows the editor by a soft-wrapped row: every row
// below reflows one column sideways, and the body gains a row.
//
// This case does NOT ride a band scroll, despite what this test used to be
// called. _grow_editor() asks for one, but present() only emits a DL/IL when
// that beats repainting the band, and here it does not: the frame is one IRM
// shift per reflowed row (ESC[4h/ESC[4l) and no scroll escape at all.
//
// Nor can the budget pin the scroll. Removing the scroll_region() call outright
// makes this frame CHEAPER, not dearer - 43 bytes against 59 - because the
// editor then renders from a stale origin instead of paying for the shift. An
// upper bound passes the broken case by construction. What survives is the
// honest claim: a head insert into a multi-row editor costs a per-row shift,
// not a body rewrite (which runs to several hundred bytes). The rendered result
// is pinned where it can be seen, by the pyte reconcile scenarios.
TEST_CASE("editor bytes: a head insert that grows the editor shifts rows, not rewrites them") {
    EditorBytes f;
    // Fill well past one row (80 cols) so the editor is several rows tall.
    for (int i = 0; i < 5; i++)
        f.type("0123456789abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOP ");
    f.flush();

    // Go to the head of the text and insert there: every following row reflows by
    // one column, and the body grows a row once it crosses the wrap boundary.
    uint16_t len = f.editor.buffer_len();
    for (uint16_t i = 0; i < len; i++) f.editor.move_left();
    f.flush();

    size_t cost = f.cost_of_type("Z");
    CHECK(cost > 0);
    CHECK(cost <= 70);          // four reflowed rows; a body rewrite is 300+
}

// Deleting the char that un-wraps a row shrinks the editor: the DL closes the
// vacated row and the rows below slide up.
TEST_CASE("editor bytes: shrinking a row rides a scroll too") {
    EditorBytes f;
    for (int i = 0; i < 3; i++)
        f.type("0123456789abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOP ");
    f.flush();

    uint16_t len = f.editor.buffer_len();
    for (uint16_t i = 0; i < len; i++) f.editor.move_left();
    f.flush();

    f.editor.delete_forward(1);     // un-wraps the tail by one column
    size_t cost = f.flush().size();
    CHECK(cost > 0);
    CHECK(cost <= 70);
}

// Enter opens a hard-broken continuation row: a one-row grow with a fresh PS2/PS3
// gutter, and the rows below slide rather than being repainted.
TEST_CASE("editor bytes: a hard break costs a scroll plus the new row") {
    EditorBytes f;
    f.type("first line");
    f.flush();

    size_t cost = f.cost_of_type("\n");
    CHECK(cost > 0);
    CHECK(cost <= 120);
}

// The case where a row moves vertically AND slides sideways at once: editing at
// the head of a multi-row input reflows every row below by a column, while the
// editor also grows or shrinks a row. Neither optimization catches a row that
// did both - the shift detection compares a row against the same row in the
// back-buffer - so the reconcile must not displace rows the reflow left in
// place. A soft-wrap row appears at the bottom, so the bottom-pinned grow has
// already positioned everything: an IL below the edit would push each reflowed
// row out from under the shift detection and cost a full rewrite each.
//
// The budgets below are per-row: one shift escape each, not one row rewrite each.
TEST_CASE("editor bytes: head edits reflow rows without rewriting them") {
    // 80 columns exactly, so each typed block fills one row with no wrap slack.
    const char* ROW = "0123456789abcdefghijklmnopqrstuvwxyz0123456789"
                      "ABCDEFGHIJKLMNOPQRSTUVWXYZ01234567";

    struct Case { int rows; size_t insert_max; size_t delete_max; };
    const Case cases[] = { {2, 60, 64}, {4, 82, 96}, {6, 104, 128} };

    for (const Case& c : cases) {
        EditorBytes f;
        for (int i = 0; i < c.rows; i++) f.type(ROW);
        f.flush();
        uint16_t len = f.editor.buffer_len();
        for (uint16_t i = 0; i < len; i++) f.editor.move_left();   // to the head
        f.flush();

        // Insert one char at the head: every row shifts right one column and the
        // input gains a row at the bottom.
        size_t ins = f.cost_of_type("Z");
        CHECK(ins > 0);
        CHECK(ins <= c.insert_max);

        // ...and delete it again: every row shifts back left, the row goes away.
        f.editor.delete_forward(1);
        size_t del = f.flush().size();
        CHECK(del > 0);
        CHECK(del <= c.delete_max);
    }
}

// A hard break is the case that genuinely does need the IL: it splits a row in
// place, so the rows below move down bodily rather than reflowing. Dropping the
// slide for reflows must not drop it here.
TEST_CASE("editor bytes: a hard break still slides the rows below it") {
    EditorBytes f;
    f.type("first line");
    f.editor.insert_characters("\n", 1);
    f.type("second line");
    f.editor.insert_characters("\n", 1);
    f.type("third line");
    f.flush();

    // Back up into the first line and break it: lines two and three slide down.
    for (uint16_t i = 0; i < 27; i++) f.editor.move_left();
    f.flush();

    size_t cost = f.cost_of_type("\n");
    CHECK(cost > 0);
    CHECK(cost <= 60);                        // an IL plus the split row, not 3 rows
    // The rows that merely slid were not re-sent.
    CHECK(f.flush().empty());                 // and it converged
}

// The same head edit at the link baud the sim (and the installation) actually
// runs at. Every case above uses a baud high enough that present()'s per-tick
// budget never binds, which is exactly what hid this: a shift refused for want
// of budget used to fall through to the run loop, whose oversized-run escape
// then emitted the full-row rewrite past the same budget - so the tail was
// repainted per keystroke precisely when bytes were scarcest. A refused shift
// now defers the row to the next tick, where the escape fits.
TEST_CASE("editor bytes: a paced link still shifts rows rather than rewriting them") {
    const char* ROW = "0123456789abcdefghijklmnopqrstuvwxyz0123456789"
                      "ABCDEFGHIJKLMNOPQRSTUVWXYZ01234567";   // 80 cols
    EditorBytes f(4800u);                       // the simulator's console baud
    for (int i = 0; i < 4; i++) f.type(ROW);
    f.flush();
    uint16_t len = f.editor.buffer_len();
    for (uint16_t i = 0; i < len; i++) f.editor.move_left();
    f.flush();

    size_t ins = f.cost_of_type("Z");
    CHECK(ins > 0);
    CHECK(ins <= 100);                          // was 148: one row rewritten whole

    f.editor.delete_forward(1);
    size_t del = f.flush().size();
    CHECK(del > 0);
    CHECK(del <= 100);                          // was 153
}

// A bulk edit (a paste, a yank, coalesced fast typing) slides a row by many
// columns at once. The shift detector must have no ceiling on the width: capping
// it turns every wider slide into a silent full-row rewrite, which is worth ~120
// bytes a keystroke on a two-row input.
TEST_CASE("editor bytes: a wide slide is still one escape") {
    const char* ROW = "0123456789abcdefghijklmnopqrstuvwxyz0123456789"
                      "ABCDEFGHIJKLMNOPQRSTUVWXYZ01234567";
    {
        EditorBytes f;
        f.type(ROW); f.type(ROW);
        f.flush();
        uint16_t len = f.editor.buffer_len();
        for (uint16_t i = 0; i < len; i++) f.editor.move_left();
        f.flush();
        size_t cost = f.cost_of_type("0123456789");     // 10-column right slide
        CHECK(cost > 0);
        CHECK(cost <= 110);                             // was 196 with a width cap
    }
    {
        EditorBytes f;
        f.type(ROW); f.type(ROW); f.type(ROW);
        f.flush();
        uint16_t len = f.editor.buffer_len();
        for (uint16_t i = 0; i < len; i++) f.editor.move_left();
        f.flush();
        f.editor.delete_forward(10);                    // 10-column left slide
        size_t cost = f.flush().size();
        CHECK(cost > 0);
        CHECK(cost <= 110);                             // was 269
    }
}

// Top-anchored viewport (the NOTES geometry)
// ----------------------------------------------------------------------------
//
// Same reconcile, but the text is now taller than the window and the window is
// what moves. Two things that could go wrong and cost a screen each: a row
// scrolled in from outside the window being spliced against glyphs it never
// had, and a viewport scroll repainting the whole band instead of the rows the
// scroll uncovered. The budgets below are what pins them.

// The editor is 8 rows over 20 rows of text, so most of the note is off screen.
// Typing into the middle must still cost a splice - the rows below it are the
// same rows, just shown through a smaller hole.
TEST_CASE("editor bytes: a mid-document insert splices, viewport or not") {
    EditorBytes f;
    f.as_viewport(8);
    f.fill_rows(20);

    for (int i = 0; i < 6; i++) f.editor.move_up();     // into the middle of the window
    f.editor.move_to_line_start();
    f.flush();

    size_t cost = f.cost_of_type("X");
    CHECK(cost > 0);
    CHECK(cost <= 28);          // an insert-mode shift on one row
}

// A hard break in the middle pushes every row below it down one. Inside a
// viewport that also drops a row off the bottom edge - which must cost nothing,
// because it is no longer on screen.
TEST_CASE("editor bytes: a hard break in a viewport slides rows, none below it") {
    EditorBytes f;
    f.as_viewport(8);
    f.fill_rows(20);

    for (int i = 0; i < 4; i++) f.editor.move_up();
    f.editor.move_to_line_end();
    f.flush();

    size_t cost = f.cost_of_type("\n");
    CHECK(cost > 0);
    CHECK(cost <= 60);          // one IL plus the row it opened; a band is 300+
}

// The reverse: joining two rows pulls the tail up, and a row that was BELOW the
// window slides into its last line. That row has no glyphs on screen, so the
// walk must repaint it rather than splice it - and repaint only it.
TEST_CASE("editor bytes: a join repaints just the row the viewport uncovers") {
    EditorBytes f;
    f.as_viewport(8);
    f.fill_rows(20);

    for (int i = 0; i < 4; i++) f.editor.move_up();
    f.editor.move_to_line_start();
    f.flush();

    f.editor.backspace(1);                              // eat the newline above
    size_t cost = f.flush().size();
    CHECK(cost > 0);
    CHECK(cost <= 70);          // one DL, one uncovered row, one merged row
}

// Walking off the TOP edge scrolls the window by one. That is a scroll plus the
// single row it revealed - not a repaint of the eight rows on screen.
TEST_CASE("editor bytes: the viewport follows the cursor by scrolling one row") {
    EditorBytes f;
    f.as_viewport(8);
    f.fill_rows(20);
    f.console.terminal().set_cursor_visible(true);

    for (int i = 0; i < 7; i++) f.editor.move_up();     // up to the top edge
    f.flush();

    f.editor.move_up();                                 // one row past it
    size_t cost = f.flush().size();
    CHECK(cost > 0);
    CHECK(cost <= 40);          // a scroll and the one row it uncovered
}

// A jump longer than the window is the case where scrolling buys nothing: no row
// on screen survives it. The cost is a repaint of the WINDOW - bounded by the
// eight rows on screen, never by the sixty in the note.
TEST_CASE("editor bytes: a long jump repaints the window, not the note") {
    EditorBytes f;
    f.as_viewport(8);
    f.fill_rows(60);
    f.console.terminal().set_cursor_visible(true);

    f.editor.move_to_start();
    f.flush();

    f.editor.move_to_end();                             // 52 rows away
    size_t cost = f.flush().size();
    CHECK(cost > 0);
    CHECK(cost <= 220);         // eight rows; the note is sixty
}

}  // namespace editor_bytes

// --------------------------------------------------------------------------
// parser
// --------------------------------------------------------------------------

namespace parser {

using namespace Botanik;
using Botanik::Terminal::InputParser;
using Botanik::Terminal::InputEvent;
using Command = InputEvent::Command;
using EventType = InputEvent::EventType;

namespace {

    // A parser wired to a pipe: bytes written into feed() land in the RX ring and
    // are decoded by parse(). `now` is the parser's shared loop clock (ms).
    struct ParserFixture {
        int                       pipe_fds[2];
        Botanik::HAL::Serial::Raw   raw;
        Botanik::Utils::BufferedSerial serial;
        uint64_t                  now = 0;
        InputParser               parser;

        ParserFixture() : serial(&raw), parser(serial, &now) {
            REQUIRE(::pipe(pipe_fds) == 0);
            raw.set_fds(pipe_fds[0], -1);     // RX reads the pipe's read end
        }
        ~ParserFixture() { ::close(pipe_fds[0]); ::close(pipe_fds[1]); }

        // Push raw bytes through the wire into the RX ring.
        void feed(const char* data, size_t n) {
            REQUIRE(::write(pipe_fds[1], data, n) == (ssize_t)n);
            serial.receive();
        }
        void feed(const char* s) { feed(s, std::strlen(s)); }

        InputEvent& parse() { return parser.parse(); }
    };

}

// Printable text
// ----------------------------------------------------------------------------

TEST_CASE("parser: printable bytes accumulate into one CONTENT batch") {
    ParserFixture f;
    f.feed("hello");
    InputEvent& e = f.parse();
    CHECK(e.type == EventType::CONTENT);
    CHECK(e.batch_len == 5);
    CHECK(std::memcmp(e.batch_data, "hello", 5) == 0);
}

TEST_CASE("parser: a full batch flushes at ANSI_INPUT_BATCH_SIZE") {
    ParserFixture f;
    char buf[Config::ANSI_INPUT_BATCH_SIZE];
    std::memset(buf, 'a', sizeof(buf));
    f.feed(buf, sizeof(buf));
    InputEvent& e = f.parse();
    CHECK(e.type == EventType::CONTENT);
    CHECK(e.batch_len == Config::ANSI_INPUT_BATCH_SIZE);
}

// C0 control bytes
// ----------------------------------------------------------------------------

TEST_CASE("parser: control bytes decode to commands") {
    ParserFixture f;
    f.feed("\x0D");                         // CR
    CHECK(f.parse().command == Command::ENTER);
    f.feed("\x0A");                         // LF
    CHECK(f.parse().command == Command::ENTER);
    f.feed("\x09");                         // TAB
    CHECK(f.parse().command == Command::TAB);
    f.feed("\x03");                         // Ctrl+C
    CHECK(f.parse().command == Command::CTRL_C);
}

// CSI sequences
// ----------------------------------------------------------------------------

TEST_CASE("parser: CSI arrow keys") {
    ParserFixture f;
    f.feed("\x1B[A"); CHECK(f.parse().command == Command::UP);
    f.feed("\x1B[B"); CHECK(f.parse().command == Command::DOWN);
    f.feed("\x1B[C"); CHECK(f.parse().command == Command::RIGHT);
    f.feed("\x1B[D"); CHECK(f.parse().command == Command::LEFT);
    f.feed("\x1B[H"); CHECK(f.parse().command == Command::HOME);
    f.feed("\x1B[F"); CHECK(f.parse().command == Command::END);
}

TEST_CASE("parser: CSI tilde keys, with and without a modifier param") {
    ParserFixture f;
    f.feed("\x1B[3~");   CHECK(f.parse().command == Command::DELETE);
    f.feed("\x1B[5~");   CHECK(f.parse().command == Command::PAGE_UP);
    f.feed("\x1B[6~");   CHECK(f.parse().command == Command::PAGE_DOWN);
    // xterm modifier: Ctrl is bit 4 of (param2 - 1), so 5;5~ is Ctrl+PageUp.
    f.feed("\x1B[5;5~"); CHECK(f.parse().command == Command::CTRL_PAGE_UP);
    f.feed("\x1B[6;5~"); CHECK(f.parse().command == Command::CTRL_PAGE_DOWN);
}

// The same modifier bitmask on the arrows, where Shift (bit 1) is what picks
// the line-at-a-time scrollback scroll out of a plain arrow.
TEST_CASE("parser: a shifted arrow is its own command") {
    ParserFixture f;
    f.feed("\x1B[1;2A"); CHECK(f.parse().command == Command::SHIFT_UP);
    f.feed("\x1B[1;2B"); CHECK(f.parse().command == Command::SHIFT_DOWN);
    // Bare arrows carry no param at all, so they must stay unshifted...
    f.feed("\x1B[A");    CHECK(f.parse().command == Command::UP);
    f.feed("\x1B[B");    CHECK(f.parse().command == Command::DOWN);
    // ...as must a modifier without the Shift bit (5 - 1 = Ctrl alone).
    f.feed("\x1B[1;5A"); CHECK(f.parse().command == Command::UP);
    // Shift+Ctrl keeps the Shift bit, so it still scrolls rather than falling
    // back to a bare arrow.
    f.feed("\x1B[1;6A"); CHECK(f.parse().command == Command::SHIFT_UP);
    // Left/Right take no shifted meaning: nothing scrolls sideways.
    f.feed("\x1B[1;2C"); CHECK(f.parse().command == Command::RIGHT);
}

TEST_CASE("parser: a long digit run saturates instead of wrapping") {
    ParserFixture f;
    // Param overflows uint16_t; it must clamp, not roll over into a small value
    // that spuriously matches a tilde key. 99999... is no valid key -> NONE.
    f.feed("\x1B[999999~");
    CHECK(f.parse().command == Command::NONE);
}

// Alt (ESC-prefixed) sequences
// ----------------------------------------------------------------------------

TEST_CASE("parser: Alt+letter and Alt+digit") {
    ParserFixture f;
    f.feed("\x1B" "b"); CHECK(f.parse().command == Command::ALT_B);
    f.feed("\x1B" "f"); CHECK(f.parse().command == Command::ALT_F);
    f.feed("\x1B" "3"); CHECK(f.parse().command == Command::ALT_3);
}

TEST_CASE("parser: a lone ESC resolves to ESC after the command timeout") {
    ParserFixture f;
    f.feed("\x1B");
    CHECK(f.parse().type == EventType::NONE);   // still waiting for the rest
    f.now += Config::ANSI_CMD_TIMEOUT_MS;       // nothing more arrives
    CHECK(f.parse().command == Command::ESC);
}

// Chunkable coalescing (held BACKSPACE / CTRL_D)
// ----------------------------------------------------------------------------

TEST_CASE("parser: a single backspace emits BACKSPACE with count 1") {
    ParserFixture f;
    f.feed("\x7F");
    InputEvent& e = f.parse();
    CHECK(e.command == Command::BACKSPACE);
    CHECK(e.count == 1);
}

TEST_CASE("parser: repeated backspaces coalesce into one count-N command") {
    ParserFixture f;
    f.feed("\x7F\x7F\x7F");
    InputEvent& e = f.parse();
    CHECK(e.command == Command::BACKSPACE);
    CHECK(e.count == 3);
}

TEST_CASE("parser: Ctrl+D is its own chunkable command") {
    ParserFixture f;
    f.feed("\x04\x04");
    InputEvent& e = f.parse();
    CHECK(e.command == Command::CTRL_D);
    CHECK(e.count == 2);
}

}  // namespace parser

// --------------------------------------------------------------------------
// line_buffer
// --------------------------------------------------------------------------

namespace line_buffer {

using Botanik::Pages::LineBuffer;

namespace {
    // True if the line @p dist back from newest equals @p expect. Calls line()
    // internally so its out-param is populated before the comparison (sibling
    // call-argument evaluation order would otherwise be unspecified).
    template <class LB>
    bool line_eq(const LB& lb, uint16_t dist, const char* expect) {
        uint16_t len = 0;
        const char* p = lb.line(dist, len);
        return p && len == (uint16_t)std::strlen(expect) && std::memcmp(p, expect, len) == 0;
    }
}

// store / line / count
// ----------------------------------------------------------------------------

TEST_CASE("LineBuffer: stores lines and reads them newest-first") {
    LineBuffer<8, 16> lb;
    CHECK(lb.count() == 0);
    lb.store("one", 3);
    lb.store("two", 3);
    lb.store("three", 5);
    CHECK(lb.count() == 3);

    CHECK(line_eq(lb, 1, "three")); // 1 = most recent
    CHECK(line_eq(lb, 2, "two"));
    CHECK(line_eq(lb, 3, "one"));
}

TEST_CASE("LineBuffer: line() rejects out-of-range distances") {
    LineBuffer<8, 16> lb;
    lb.store("x", 1);
    uint16_t len = 7;
    CHECK(lb.line(0, len) == nullptr);   // 0 is not a valid 1-based distance
    CHECK(lb.line(2, len) == nullptr);   // only one line stored
    CHECK(len == 7);                     // len untouched on failure
}

TEST_CASE("LineBuffer: a line longer than N is truncated to N bytes") {
    LineBuffer<4, 8> lb;
    lb.store("0123456789ABC", 13);       // 13 bytes into 8-wide slots
    uint16_t len = 0;
    const char* p = lb.line(1, len);
    CHECK(len == 8);
    CHECK(std::memcmp(p, "01234567", 8) == 0);
}

TEST_CASE("LineBuffer: wraps once full, evicting the oldest lines") {
    LineBuffer<3, 8> lb;
    for (const char* s : {"1", "2", "3", "4", "5"}) lb.store(s, 1);
    CHECK(lb.count() == 3);              // capped at L

    uint16_t len = 0;
    CHECK(line_eq(lb, 1, "5"));
    CHECK(line_eq(lb, 2, "4"));
    CHECK(line_eq(lb, 3, "3"));
    CHECK(lb.line(4, len) == nullptr);  // "1" and "2" were evicted
}

TEST_CASE("LineBuffer: clear() drops everything") {
    LineBuffer<4, 8> lb;
    lb.store("a", 1);
    lb.store("b", 1);
    lb.clear();
    CHECK(lb.count() == 0);
    uint16_t len = 0;
    CHECK(lb.line(1, len) == nullptr);
}

// store_wrapped
// ----------------------------------------------------------------------------

TEST_CASE("store_wrapped: splits on embedded newlines, one entry per line") {
    LineBuffer<8, 16> lb;
    uint16_t n = lb.store_wrapped("a\nb\nc", 5, /*cols*/0, "", 0);
    CHECK(n == 3);
    CHECK(lb.count() == 3);
    CHECK(line_eq(lb, 1, "c"));  // newline consumed, not printed
    CHECK(line_eq(lb, 2, "b"));
    CHECK(line_eq(lb, 3, "a"));
}

TEST_CASE("store_wrapped: breaks on width and prefixes continuation rows") {
    LineBuffer<8, 16> lb;
    uint16_t n = lb.store_wrapped("abcdef", 6, /*cols*/4, ">", 1);
    CHECK(n == 2);
    CHECK(line_eq(lb, 2, "abcd")); // first row: no prefix, full width
    CHECK(line_eq(lb, 1, ">ef"));  // continuation: prefix + remainder
}

TEST_CASE("store_wrapped: cols == 0 means unbounded width (newlines still split)") {
    LineBuffer<8, 32> lb;
    uint16_t n = lb.store_wrapped("a long single line", 18, /*cols*/0, ">", 1);
    CHECK(n == 1);                                // no width cap -> one entry
    CHECK(line_eq(lb, 1, "a long single line"));
}

TEST_CASE("store_wrapped: always stores at least one (possibly empty) line") {
    LineBuffer<8, 16> lb;
    uint16_t n = lb.store_wrapped("", 0, 0, "", 0);
    CHECK(n == 1);
    CHECK(lb.count() == 1);
    uint16_t len = 7;
    const char* p = lb.line(1, len);
    CHECK(p != nullptr);
    CHECK(len == 0);
}

}  // namespace line_buffer

// --------------------------------------------------------------------------
// scrollback
// --------------------------------------------------------------------------

namespace scrollback {

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

}  // namespace scrollback

// --------------------------------------------------------------------------
// shell
// --------------------------------------------------------------------------

namespace shell {

using namespace Botanik;
using namespace Botanik::Vars;

namespace {

    // Sinks command output to nowhere; the tests only inspect the editor buffer.
    struct NullWriter : Shell::ShellWriter {
        size_t write(const char* /*buf*/, size_t len) override { return len; }
    };

    // ...and one that keeps it, for the tests that are about what a command SAYS.
    struct CapturingWriter : Shell::ShellWriter {
        std::string text;
        size_t write(const char* buf, size_t len) override {
            text.append(buf, len); text.push_back('\n'); return len;
        }
        bool has(const char* s) const { return text.find(s) != std::string::npos; }
    };

    // A CommandShell wired to a real editor + terminal whose serial is a pipe (TX is
    // buffered and never flushed, so reconcile output is harmlessly discarded).
    struct ShellFixture {
        int                            pipe_fds[2];
        HAL::Serial::Raw                 raw;
        Utils::BufferedSerial          serial;
        uint64_t                       now = 0;
        Terminal::Console              console;
        Terminal::EditorBuffers<Config::SHELL_INPUT_LEN,
                                Config::EDITOR_MAX_ROWS> bufs;
        Terminal::TextEditor           editor;
        NullWriter                     writer;
        Shell::CommandShell            shell;

        ShellFixture()
            : serial(&raw),
              console(serial, 80, 24, &now, 115200),
              editor(&console, bufs.storage()),
              shell(console.terminal(), editor, writer) {
            REQUIRE(::pipe(pipe_fds) == 0);
            raw.set_fds(pipe_fds[0], pipe_fds[1]);
            editor.reset();
        }
        ~ShellFixture() { ::close(pipe_fds[0]); ::close(pipe_fds[1]); }

        std::string line() const { return std::string(editor.buffer(), editor.buffer_len()); }
    };

}

TEST_CASE("shell history: prev/next recall with draft restore") {
    ShellFixture f;
    f.shell.run("version", 7);
    f.shell.run("uptime", 6);

    f.shell.history_prev();              // newest
    CHECK(f.line() == "uptime");
    f.shell.history_prev();              // older
    CHECK(f.line() == "version");
    f.shell.history_prev();              // already oldest: no change
    CHECK(f.line() == "version");
    f.shell.history_next();              // newer
    CHECK(f.line() == "uptime");
    f.shell.history_next();              // back to the (empty) draft
    CHECK(f.line() == "");
}

TEST_CASE("shell history: an in-progress line is saved as the draft and restored") {
    ShellFixture f;
    f.shell.run("stats", 5);
    f.editor.insert_characters("dra", 3);   // in-progress line
    f.shell.history_prev();                  // saves "dra", recalls "stats"
    CHECK(f.line() == "stats");
    f.shell.history_next();                  // restores the draft
    CHECK(f.line() == "dra");
}

TEST_CASE("shell history: consecutive duplicates are not stored twice") {
    ShellFixture f;
    f.shell.run("help", 4);
    f.shell.run("help", 4);              // duplicate of the most recent: skipped
    f.shell.history_prev();
    CHECK(f.line() == "help");
    f.shell.history_prev();              // only one distinct entry: no older
    CHECK(f.line() == "help");
}

TEST_CASE("shell kill ring: Ctrl-K cuts to EOL, Ctrl-Y yanks it back") {
    ShellFixture f;
    f.editor.insert_characters("hello world", 11);
    for (int i = 0; i < 5; ++i) f.editor.move_left();   // cursor before "world"
    f.shell.kill_to_line_end();
    CHECK(f.line() == "hello ");
    f.editor.move_to_line_end();
    f.shell.paste_kill();
    CHECK(f.line() == "hello world");
}

// ---------------------------------------------------------------------------
// The variable commands, driven against a real bank so the output is the real
// output.
// ---------------------------------------------------------------------------

namespace {

    const Vars::Variable CMD_VARS[] = {
        MI("ext_temp", T_F32, 5, 3, 18, 2, SF(-40.0f), SF(85.0f), SF(20.5f), 0, 1,
                       "C", "outside", ""),
        G ("setpoint", T_F32, SF(18.0f), 1, "C", "target temp", ""),
    };

    /** @brief A shell with a bank behind it and its output kept. */
    struct CmdFixture {
        int                            pipe_fds[2];
        HAL::Serial::Raw               raw;
        Utils::BufferedSerial          serial;
        uint64_t                       now = 0;
        Terminal::Console              console;
        Terminal::EditorBuffers<Config::SHELL_INPUT_LEN,
                                Config::EDITOR_MAX_ROWS> bufs;
        Terminal::TextEditor           editor;
        CapturingWriter                writer;
        Vars::Value                    values[2]{};
        uint16_t                       index[2]{};
        Vars::Bank                     bank;
        // Declared before the shell so its address is valid in the init list;
        // the fields are filled in the body, which is what the shell reads.
        SystemContext                  ctx{};
        Shell::CommandShell            shell;

        CmdFixture()
            : serial(&raw),
              console(serial, 80, 24, &now, 115200),
              editor(&console, bufs.storage()),
              shell(console.terminal(), editor, writer, &ctx) {
            REQUIRE(::pipe(pipe_fds) == 0);
            raw.set_fds(pipe_fds[0], pipe_fds[1]);
            editor.reset();
            Vars::VarTables t = {};
            t.var = CMD_VARS; t.val = values; t.count = 2; t.index = index;
            bank = Vars::Bank(t);
            bank.init();
            ctx.vars = &bank;
        }
        ~CmdFixture() { ::close(pipe_fds[0]); ::close(pipe_fds[1]); }

        void run(const char* cmd) {
            writer.text.clear();
            shell.run(cmd, (uint16_t)std::strlen(cmd));
        }
    };

}

TEST_CASE("shell override: a bare name forces a GOOD reading; `clear` releases all") {
    // Freezing used to carry the row's current quality through, so a bare
    // `override` on a variable nothing had written yet handed back an override
    // that read as a dead sensor. Only the word `invalid` says that.
    CmdFixture f;
    REQUIRE_FALSE(f.bank.var("ext_temp").valid());   // nothing has published it

    f.run("override ext_temp");
    CHECK(f.bank.var("ext_temp").valid());
    f.run("override ext_temp invalid");
    CHECK_FALSE(f.bank.var("ext_temp").valid());

    f.run("override setpoint 5");
    REQUIRE(f.bank.override_count() == 2);
    f.run("override clear");
    CHECK(f.writer.has("cleared 2"));
    CHECK(f.bank.override_count() == 0);
}

TEST_CASE("shell get: the detail view, and what it adds when a row is forced") {
    CmdFixture f;
    f.bank.var("ext_temp").publish(Vars::Scalar::F(21.0f), 1000);

    f.run("get ext_temp");
    MESSAGE("get ext_temp ->\n" << f.writer.text);
    CHECK(f.writer.has("ext_temp = 21.0"));
    CHECK(f.writer.has(": MI"));                  // what kind of row...
    CHECK(f.writer.has("(input, direct)"));       // ...spelled out
    CHECK(f.writer.has("Group"));
    CHECK(f.writer.has(": read-only"));           // and whether `set` takes it
    CHECK(f.writer.has("Raw value"));
    CHECK(f.writer.has("21.0 C   (valid)"));
    CHECK(f.writer.has("-40.0 .. 85.0"));         // the declared band
    CHECK(f.writer.has("Modbus dev 5"));          // where it comes from
    CHECK(f.writer.has("Override value .............. : -"));   // nothing forced

    // Forced: both values, kept apart, each saying whether it is worth
    // believing. "Has the sensor come back?" is the question someone deciding
    // whether to release an override actually has.
    f.run("override ext_temp 35");
    f.run("get ext_temp");
    MESSAGE("get ext_temp (forced) ->\n" << f.writer.text);
    CHECK(f.writer.has("Raw value ................... : 21.0 C   (valid)"));
    CHECK(f.writer.has("Override value .............. : 35.0 C   (valid)"));

    // ...and forcing the QUALITY shows on the value it belongs to.
    f.run("override ext_temp 35 invalid");
    f.run("get ext_temp");
    CHECK(f.writer.has("Override value .............. : 35.0 C   (invalid)"));
}

}  // namespace shell
