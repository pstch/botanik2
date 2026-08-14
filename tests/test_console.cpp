/**
 * @file test_console.cpp
 * @brief The console: terminal diff/present, the editor, input parsing, scrollback, shell.
 *
 * Each section is its own namespace so fixtures and helpers cannot collide.
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
#include "term_fixture.h"                  // botanik_test::PipeSerial
#include "libbotanik/console/console.h" // Console
#include "libbotanik/console/editor.h"  // Terminal::TextEditor
#include "libbotanik/console/parser.h"  // Terminal::InputParser
#include "libbotanik/pages/line_buffer.h"   // Pages::LineBuffer
#include "libbotanik/pages/scroll_table.h"  // Pages::ScrollTablePage
#include "libbotanik/pages/scrollback.h"    // Pages::Scrollback
#include "libbotanik/shell/shell.h" // Shell::CommandShell
#include "libbotanik/tasks/tasks.h"         // Tasks::Registry - the beat the console registers in
#include "libbotanik/vars/declare.h"
#include "libbotanik/vars/params.h"         // Vars::get_int
#include "site_bank.h"                      // TestSite::build - the site's rows in a bank

// --------------------------------------------------------------------------
// terminal_present
// --------------------------------------------------------------------------

namespace terminal_present {

using namespace Botanik;

namespace {
    /** @brief A bare AnsiTerminal over a pipe, drained into a string. */
    struct TermFixture {
        botanik_test::PipeSerial wire;
        Terminal::AnsiTerminal   term;

        // Small screen keeps a row cheap to assert; default baud keeps the byte
        // budget from throttling (budget = baud * TX_COLD_TARGET_MS / 10000).
        explicit TermFixture(uint16_t cols = 20, uint16_t rows = 10,
                             uint32_t baud = 4000000u)
            : term(wire.serial, cols, rows, baud) {
            term.establish_screen();                // blank both grids
            capture();                              // discard the bring-up bytes
        }

        std::string capture() { return wire.capture(); }
        /** @brief The serial itself, to stuff the TX ring and starve the budget. */
        Utils::BufferedSerial& serial_ref() { return wire.serial; }
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

// A changed cell costs a move plus a glyph; an unchanged frame costs nothing.
TEST_CASE("present: one changed cell emits only that cell") {
    TermFixture f;
    f.put(2, 5, "X");
    std::string out = f.present();

    CHECK(out.find('X') != std::string::npos);
    CHECK(out.size() < 24);        // move + glyph + cursor park, not a row
    CHECK(f.present().empty());        // settled: a second frame writes nothing
}

// A frame over budget emits what fits and leaves the rest in FRONT for later
// frames (a run bigger than the whole budget still goes out whole).
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

// A band scroll rides a DL/IL: BACK shifts to mirror the wire, so the diff then
// repaints only the revealed row, not the whole band.
TEST_CASE("present: a band scroll emits a DL/IL, not a band repaint") {
    TermFixture f(20, 10);
    for (uint16_t r = 1; r <= 6; r++) {
        char line[8]; snprintf(line, sizeof line, "row%u", (unsigned)r);
        f.put(r, 0, line);
    }
    f.settle();                        // screen established and quiet

    // Rows 1..5 now hold what 2..6 held; row 6 is revealed as "row7".
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

// A run is droppable and must be emitted only when it fits whole: a stuffed ring
// must write nothing rather than a torn half-row.
TEST_CASE("present: a run that cannot fit the ring is deferred whole") {
    TermFixture f;
    // Stuff the TX ring so no run can fit ("HELLO" costs ~8 + 5 bytes).
    const char pad = '.';
    while (f.serial_ref().availableForWrite() > 12) f.serial_ref().write(&pad, 1);

    f.put(2, 0, "HELLO");
    f.term.present();                  // must not emit a partial run
    f.capture();                       // drain the stuffing (and anything else)

    std::string out = f.settle();      // now there is room
    CHECK(out.find("HELLO") != std::string::npos);
    CHECK(f.present().empty());        // converged
}

// With no renderer requesting a visible cursor, a settled frame parks the real
// cursor at bottom-right rather than wherever the last run ended.
TEST_CASE("present: the idle cursor parks in the bottom-right corner") {
    TermFixture f(20, 10);
    f.put(3, 4, "mid-screen");
    f.settle();
    CHECK(f.present().empty());        // already parked: a quiet frame is free
    f.put(3, 4, "MID-SCREEN");
    std::string out = f.settle();
    CHECK(out.find("MID")    != std::string::npos);   // two runs, not one string:
    CHECK(out.find("SCREEN") != std::string::npos);   // the '-' between is unchanged
    CHECK(out.find("MID-SCREEN") == std::string::npos);
    CHECK(out.back() == 'H');          // ends on an absolute CUP (ESC[r;cH)
}

// A row shifted sideways is worst-case for a plain cell diff (every column past
// the shift differs); _shift_row spots the slide and sends one escape instead.
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

// Control for the two tests below: with every row in one class, row 0 wins the
// budget on plain top-down order - so those tests aren't passing by accident.
TEST_CASE("present: without priorities the top row wins the budget") {
    TermFixture f(20, 10, 1200u);
    f.settle();

    f.put(0, 0, "12:34:56");
    f.put(4, 0, "PAGECONTENT");
    std::string first = f.present();

    CHECK(first.find("12:34:56")   != std::string::npos);
    CHECK(first.find("PAGECONTENT") == std::string::npos);
}

// The alarm strip sits at the bottom, so top-down order alone would draw it
// after the page it frames. PRIO_CHROME must outrank page structure instead.
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

// "fits the budget" and "too big to ever fit" must overlap: tx_used() counts a
// byte in use even on an empty ring, so a run at exactly full depth must not fall
// through the gap between them.
TEST_CASE("present: a run costing exactly the tick budget still goes out") {
    TermFixture f(20, 10, 1200u);        // depth = 1200 * 128 / 10000 = 15 bytes
    f.settle();
    // A leading absolute move is budgeted at 8, so 7 glyphs costs exactly 15.
    f.put(3, 0, "SEVENCH");
    std::string out = f.settle();
    CHECK(out.find("SEVENCH") != std::string::npos);
    CHECK(f.present().empty());          // and it converged rather than looping
}

// A screenful of same-priority rows updating every tick: walking the class
// top-down must not let the upper rows spend the whole budget and starve the rest.
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
    // Wide enough that two rows cannot share a tick at BAUD_2ROWS, parking the
    // walk mid-frame.
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

// A single frame must read top-down: resuming a parked walk must never *wrap*
// within the tick and carry rows N..max then 0..N-1.
TEST_CASE("present: one frame never emits a row before one above it") {
    TermFixture f(20, 12, BAUD_2ROWS);
    strand_walk_at(f, 11);               // budget died on the last row

    f.put(1, 0, "TOP");                  // now dirty a row above the parked walk
    std::string one = f.present();       // ...and take a single frame

    size_t top = one.find("TOP"), bottom = one.find(WIDE);
    INFO("a frame carried row 11 before row 1");
    CHECK((top == std::string::npos || bottom == std::string::npos || top < bottom));
}

// A parked walk outlives the page that drew it, so a switch must call
// restart_scan() or the incoming page starts painting where the outgoing
// page's budget ran out.
TEST_CASE("present: restart_scan sends the walk back to the top") {
    TermFixture f(20, 12, BAUD_2ROWS);
    strand_walk_at(f, 11);

    f.term.restart_scan();
    f.put(1, 0, "TOP");
    std::string one = f.present();

    CHECK(one.find("TOP") != std::string::npos);   // the top row leads again
}

// A band scroll (the shell/logs tail) must paint its freed bottom slots
// oldest-first, which needs scroll_region() to restart the walk rather than
// resume it below them.
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

// Chrome is not one contiguous band (status row, legend, alarm strip), so
// reaching the bottom of it does not mean the class is done.
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

// A row's priority class is a standing property, not raised per-draw, so a page
// switch must hand it back cleanly: Console resets the band to plain structure
// and the incoming page reclaims its own rows in focus().
TEST_CASE("present: reclassifying a row hands it back to the page") {
    TermFixture f(20, 10, 1200u);        // budget ~ one row
    f.settle();

    f.term.set_row_priority(6, Terminal::PRIO_INPUT);   // row 6: keystroke echo
    f.put(6, 0, "EDITROW");
    f.put(2, 0, "PAGEROW");
    std::string out = f.present();
    CHECK(out.find("EDITROW") < out.find("PAGEROW"));   // echo goes first
    f.settle();

    f.term.set_row_priority(1, 9, Terminal::PRIO_STRUCT);   // page switch: reset
    f.put(6, 0, "was-edit");
    f.put(2, 0, "now-page");
    out = f.present();
    CHECK(out.find("now-page") != std::string::npos);   // top-down again
    CHECK(out.find("was-edit") == std::string::npos);
}

// A scroll must mirror into BACK only if the whole DL/IL reached the wire, or
// the diff believes those rows are already correct and never repaints them.
TEST_CASE("present: a scroll whose escape does not fit leaves BACK alone") {
    TermFixture f(20, 10);
    for (uint16_t r = 1; r <= 6; r++) {
        char line[8]; snprintf(line, sizeof line, "row%u", (unsigned)r);
        f.put(r, 0, line);
    }
    f.settle();

    // Stuff the ring so neither the DL nor its compensating IL can be emitted.
    const char pad = '.';
    while (f.serial_ref().availableForWrite() > 4) f.serial_ref().write(&pad, 1);

    f.term.scroll_region(1, 6, /*up=*/true, 1);
    for (uint16_t r = 1; r <= 6; r++) {
        char line[8]; snprintf(line, sizeof line, "row%u", (unsigned)(r + 1));
        f.put(r, 0, line);
    }
    f.term.present();          // the scroll cannot go out
    f.capture();               // drain the stuffing

    // The band must still converge: BACK left unshifted, the diff repaints
    // whatever really differs. A wrongly-mirrored BACK would leave FRONT and
    // the wire disagreeing forever, with present() going quiet on a wrong band.
    std::string out = f.settle();
    CHECK(out.size() > 0);              // the band was reconciled somehow
    CHECK(f.present().empty());         // and FRONT now matches BACK
}

// reconfigure() - the device half of the `term_` parameter group.

TEST_CASE("reconfigure: a new geometry takes, and blanks what addressed the old one") {
    TermFixture f(20, 10);
    f.put(3, 0, "0123456789");
    f.settle();

    REQUIRE(f.term.reconfigure(f.term.terminal_type(), 40, 12));
    CHECK(f.term.cols() == 40);
    CHECK(f.term.rows_total() == 12);

    // Every cell's meaning moved with _cols, so nothing may survive into the
    // new screen: both grids are blank and agree, so the frame has no CONTENT
    // to emit. (It still parks the idle cursor, whose corner just moved, so
    // this is not an empty string.)
    f.capture();
    const std::string first = f.present();
    INFO("emitted after the resize: " << first);
    CHECK(first.find("0123456789") == std::string::npos);
    CHECK(f.present().empty());          // ...and by the second frame, settled

    INFO("and the wider row is now reachable");
    f.put(3, 35, "X");
    CHECK(f.present().size() > 0);
}

TEST_CASE("reconfigure: a no-op call changes nothing and emits nothing") {
    TermFixture f(20, 10);
    f.put(1, 1, "hello");
    f.settle();

    CHECK_FALSE(f.term.reconfigure(f.term.terminal_type(), 20, 10));
    INFO("the screen was not blanked, so the painted row is still on the wire");
    CHECK(f.capture().empty());
    CHECK(f.present().empty());
}

TEST_CASE("reconfigure: a geometry past the grid is clamped, not honoured") {
    TermFixture f(20, 10);
    // The parameter band stops this, but the band is the only guard and a
    // corrupt store reaches the device by another road. Clamped exactly as the
    // constructor clamps, or _idx() walks off both grids.
    REQUIRE(f.term.reconfigure(f.term.terminal_type(), 60000, 60000));
    CHECK(f.term.cols() == Config::TERMINAL_MAX_COLS);
    CHECK(f.term.rows_total() == Config::TERMINAL_MAX_ROWS);

    SUBCASE("and a zero is a terminal of one, never a division by it") {
        REQUIRE(f.term.reconfigure(f.term.terminal_type(), 0, 0));
        CHECK(f.term.cols() == 1);
        CHECK(f.term.rows_total() == 1);
    }
}

TEST_CASE("reconfigure: swapping the make re-emits its setup handshake") {
    TermFixture f(20, 10);
    REQUIRE(f.term.terminal_type() == Terminal::Ansi::TerminalType::VT102);

    // A Minitel needs its PRO2 sequences; a make swap without them leaves the
    // terminal in whatever mode the last one put it in.
    REQUIRE(f.term.reconfigure(Terminal::Ansi::TerminalType::MINITEL_1B, 40, 24));
    CHECK(f.term.terminal_type() == Terminal::Ansi::TerminalType::MINITEL_1B);
    const std::string out = f.capture();
    INFO("PRO2 is an ESC 0x3A sequence: <ESC>: ...");
    CHECK(out.find("\x1b:") != std::string::npos);
}

}  // namespace terminal_present

// --------------------------------------------------------------------------
// editor_bytes
// --------------------------------------------------------------------------

namespace editor_bytes {

using namespace Botanik;

namespace {

    // A TextEditor over a real Console; high baud so pacing never truncates a frame.
    struct EditorBytes {
        botanik_test::PipeSerial wire;
        uint64_t              now = 0;
        Terminal::Console     console;
        Terminal::EditorBuffers<Config::SHELL_INPUT_LEN,
                                Config::EDITOR_MAX_ROWS> bufs;
        Terminal::TextEditor  editor;

        explicit EditorBytes(uint32_t baud = 4000000u)
            : console(Botanik::SetupContext(), wire.serial, 80, 24, &now, baud),
              editor(&console, bufs.storage()) {
            console.terminal().establish_screen();
            editor.reset();
            flush();                 // discard bring-up + the initial prompt
        }

        /** @brief Re-pin as a top-anchored viewport `rows` tall with the NOTES
         *  gutter. Call before typing anything. */
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

        /** @brief Type `n` hard-broken rows "lineNN", flushing as we go. Bounded
         *  to 100: unbounded, the format is an 11-digit int into 8 bytes. */
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

        /** @brief Present until settled and return everything that reached the pipe. */
        std::string flush() {
            std::string out;
            char buf[8192];
            for (int i = 0; i < 40; i++) {
                console.terminal().present();
                for (int j = 0; j < 8; j++) wire.serial.transmit();
                ssize_t n;
                while ((n = ::read(wire.fds[0], buf, sizeof buf)) > 0) out.append(buf, (size_t)n);
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

// The common case: appending must stay one glyph plus the cursor park.
TEST_CASE("editor: a top-anchored viewport claims only its edit point as input") {
    // PRIO_INPUT outranks PRIO_CHROME, so a full-screen editor claiming its
    // whole viewport put the NOTES text on the wire ahead of the bars framing
    // it - the one page whose frame arrived last.
    EditorBytes f;
    f.as_viewport(10);                      // rows 1..10, the NOTES geometry
    f.fill_rows(4);

    const Terminal::AnsiTerminal& t = f.console.terminal();
    CHECK(t.row_priority(0) == Terminal::PRIO_CHROME);   // the status bar still outranks

    uint16_t input_rows = 0;
    for (uint16_t r = 1; r <= 10; r++)
        if (t.row_priority(r) == Terminal::PRIO_INPUT) input_rows++;
    INFO("the edit point is one row; the rest of the note is structure");
    CHECK(input_rows == 1);
}

TEST_CASE("editor bytes: appending a char costs about a char") {
    EditorBytes f;
    f.type("hello");
    f.flush();

    size_t cost = f.cost_of_type("!");
    CHECK(cost > 0);
    CHECK(cost <= 20);
}

// A head insert shifts everything right; the terminal must send one insert
// escape, not rewrite the whole tail.
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

// A head insert that grows the editor by a soft-wrapped row must cost a per-row
// IRM shift, not a several-hundred-byte body rewrite.
TEST_CASE("editor bytes: a head insert that grows the editor shifts rows, not rewrites them") {
    EditorBytes f;
    for (int i = 0; i < 5; i++)   // several rows tall (past 80 cols each)
        f.type("0123456789abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOP ");
    f.flush();

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

// A row that moves vertically AND slides sideways: shift detection compares a
// row against the SAME row in the back-buffer, so unmoved rows must not be
// displaced by an IL. Budgets are per-row: one shift escape each.
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

// A hard break genuinely needs the IL: it splits a row in place and moves the
// rows below down bodily, unlike a reflow.
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

// At the installation's real baud, where the budget actually binds: a shift
// refused for want of budget must defer to the next tick, not fall through to
// a full-row rewrite.
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

// A bulk edit (paste, yank, coalesced typing) slides a row by many columns at
// once; the shift detector must have no ceiling on the width.
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
// Text taller than the window, and the window moves. Two failure modes cost a
// screen each: a scrolled-in row spliced against glyphs it never had, and a
// scroll repainting the whole band instead of just the rows it uncovered.

// Typing into the middle of an 8-row window over 20 rows of text must still
// cost a splice, not a repaint of the smaller hole it is shown through.
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

// A hard break pushes every row below it down one; the row it drops off the
// bottom edge must cost nothing, since it is no longer on screen.
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

// The reverse: joining pulls a row from BELOW the window into view with no
// glyphs on screen, so the walk must repaint only it, not splice it.
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

// Walking off the TOP edge must cost a scroll plus the one revealed row, not a
// repaint of the eight rows on screen.
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

// A jump longer than the window means scrolling buys nothing; the cost is a
// repaint bounded by the WINDOW, never by the note's full length.
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

// The region re-stamps every visible row on every motion, affordable only
// because present() diffs CELLS: dragging it one character flips one cell's
// attribute, not the other 600-odd.
TEST_CASE("editor bytes: extending a region costs a cell, not a window") {
    EditorBytes f;
    f.as_viewport(8);
    f.fill_rows(20);
    f.console.terminal().set_cursor_visible(true);

    f.editor.set_mark();
    f.flush();                                          // the anchor cell

    for (int i = 0; i < 4; i++) {
        f.editor.move_left();
        size_t cost = f.flush().size();
        CHECK(cost > 0);
        CHECK(cost <= 30);      // an SGR pair, a glyph, a cursor park
    }
    // Releasing it un-inverts the run: one row's worth, not eight.
    f.editor.clear_mark();
    size_t release = f.flush().size();
    CHECK(release > 0);
    CHECK(release <= 40);
    CHECK(f.flush().empty());   // and it converged
}

// With no mark, _sync_region() must return without touching a cell: an ordinary
// edit costs the same as if there were no region at all.
TEST_CASE("editor bytes: no mark means the region pass is not there") {
    EditorBytes f;
    f.type("hello");
    f.flush();

    size_t cost = f.cost_of_type("!");
    CHECK(cost > 0);
    CHECK(cost <= 20);
    CHECK(f.flush().empty());
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

    // A parser wired to a pipe: feed() lands bytes in the RX ring for parse() to
    // decode. `now` is the parser's shared loop clock (ms).
    struct ParserFixture {
        botanik_test::PipeSerial wire;
        uint64_t                  now = 0;
        InputParser               parser;

        ParserFixture() : parser(wire.serial, &now) {
            wire.raw.set_fds(wire.fds[0], -1);   // RX only: fed, never drained
        }

        void feed(const char* data, size_t n) {
            REQUIRE(::write(wire.fds[1], data, n) == (ssize_t)n);
            wire.serial.receive();
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
    // The three cut/paste keys.
    f.feed("\x0B");                         // Ctrl+K
    CHECK(f.parse().command == Command::CTRL_K);
    f.feed("\x17");                         // Ctrl+W
    CHECK(f.parse().command == Command::CTRL_W);
    f.feed("\x19");                         // Ctrl+Y
    CHECK(f.parse().command == Command::CTRL_Y);
}

// CSI sequences
// ----------------------------------------------------------------------------

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

TEST_CASE("parser: a lone ESC resolves to ESC after the command timeout") {
    ParserFixture f;
    f.feed("\x1B");
    CHECK(f.parse().type == EventType::NONE);   // still waiting for the rest
    f.now += Config::ANSI_CMD_TIMEOUT_MS;       // nothing more arrives
    CHECK(f.parse().command == Command::ESC);
}

// Chunkable coalescing (held BACKSPACE / CTRL_D)
// ----------------------------------------------------------------------------

TEST_CASE("parser: repeated backspaces coalesce into one count-N command") {
    ParserFixture f;
    f.feed("\x7F\x7F\x7F");
    InputEvent& e = f.parse();
    CHECK(e.command == Command::BACKSPACE);
    CHECK(e.count == 3);
}

}  // namespace parser

// --------------------------------------------------------------------------
// line_buffer
// --------------------------------------------------------------------------

namespace line_buffer {

using Botanik::Pages::LineBuffer;

namespace {
    // True if the line @p dist back from newest equals @p expect. line() is
    // called first: inlined into the comparison, argument order is unspecified.
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

    // 5 rows of a 10-row screen at row 1: chrome above/below a scroll must not touch.
    const uint16_t FIRST_ROW = 1;
    const uint16_t LOG_ROWS  = 5;

    /** @brief A Scrollback over a pipe-backed terminal, drained into a string. */
    struct ScrollFixture {
        botanik_test::PipeSerial wire;
        Terminal::AnsiTerminal  term;
        Pages::Scrollback<16>   view;   // enough to scroll through, few enough to wrap

        // High baud: these cases assert what a scroll paints, not how it is paced.
        ScrollFixture() : term(wire.serial, 20, 10, 4000000u) {
            term.establish_screen();                // blank both grids
            capture();                              // discard the bring-up bytes
        }

        /** @brief Push the TX ring to the pipe and return what accumulated. */
        std::string capture() {
            std::string out;
            char buf[8192];
            for (int i = 0; i < 64; i++) wire.serial.transmit();
            ssize_t n;
            while ((n = ::read(wire.fds[0], buf, sizeof buf)) > 0) out.append(buf, (size_t)n);
            return out;
        }
        /** @brief One frame: reconcile FRONT to the wire and read the bytes. */
        std::string frame() { term.present(); return capture(); }

        /** @brief Blank both grids so the next frame reports every painted cell,
         *  not its diff against what was there (e.g. a full line, not two digits). */
        void forget_screen() { term.establish_screen(); capture(); }

        /** @brief Bank line "Lnn" as the live tail does. */
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
         *  if the frame drew no tag. */
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

// The offset anchors to the newest line, so scrolling n exposes the n lines
// just older than the window's top.
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

// A line banked under a frozen view must advance the offset by one, or it
// drifts from what the window actually shows and the next scroll misaligns.
TEST_CASE("scrollback: a frozen view keeps its place as lines arrive under it") {
    ScrollFixture f;
    f.fill(12);
    CHECK(f.scroll_up(2));            // window on L05..L09, two newer below
    f.frame();

    f.feed(12);                       // two more arrive while scrolled back
    f.feed(13);
    CHECK(f.view.drain_tail(f.term, FIRST_ROW, LOG_ROWS) == false);  // frozen
    CHECK(f.frame().empty());         // ...so the window does not move

    CHECK(f.scroll_up(2));   // continues from L05, not the stale L07
    std::string out = f.frame();
    CHECK(has(out, "L03"));
    CHECK(has(out, "L04"));
    CHECK_FALSE(has(out, "L06"));
}

// A view read out from under by the ring must re-anchor on the oldest line
// left and repaint, not leave evicted text on screen.
TEST_CASE("scrollback: a view left behind by the ring re-anchors on the oldest line") {
    ScrollFixture f;
    f.fill(12);
    CHECK(f.scroll_up(5));
    f.frame();

    // 20 more: the 16-line ring wraps past everything the view was parked on.
    for (uint16_t i = 12; i < 32; i++) f.feed(i);

    f.forget_screen();                // report painted cells, not a diff
    CHECK(f.scroll_up(3));
    std::string out = f.frame();
    CHECK(has(out, "L16"));           // the oldest of the 16 still stored...
    CHECK(has(out, "L20"));           // ...through the bottom of the window
}

// scroll_to_bottom() is what every key but PAGE_UP/PAGE_DOWN calls to end the freeze.
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

// Free at the bottom, since the shell calls this on every keystroke.
TEST_CASE("scrollback: scroll_to_bottom is free at the live bottom") {
    ScrollFixture f;
    f.fill(12);
    CHECK(f.view.scroll_to_bottom(f.term, FIRST_ROW, LOG_ROWS) == false);
    CHECK(f.frame().empty());
}

// Nothing to scroll must not leave a stale offset holding the tail frozen forever.
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

    // A CommandShell over a real editor + terminal; TX is buffered and never
    // flushed, so reconcile output is harmlessly discarded.
    struct ShellFixture {
        botanik_test::PipeSerial wire;
        uint64_t                       now = 0;
        Terminal::Console              console;
        Terminal::EditorBuffers<Config::SHELL_INPUT_LEN,
                                Config::EDITOR_MAX_ROWS> bufs;
        Terminal::TextEditor           editor;
        NullWriter                     writer;
        Shell::CommandShell            shell;

        ShellFixture()
            : console(Botanik::SetupContext(), wire.serial, 80, 24, &now, 115200),
              editor(&console, bufs.storage()),
              shell(console.terminal(), editor, writer) {
            editor.reset();
        }

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

TEST_CASE("kill ring: Ctrl-K cuts to EOL, Ctrl-Y yanks it back") {
    ShellFixture f;
    f.editor.insert_characters("hello world", 11);
    for (int i = 0; i < 5; ++i) f.editor.move_left();   // cursor before "world"
    CHECK(f.editor.kill_to_line_end());
    CHECK(f.line() == "hello ");
    f.editor.move_to_line_end();
    CHECK(f.editor.yank());
    CHECK(f.line() == "hello world");
}

TEST_CASE("kill ring: Ctrl-K stops at the newline, and then eats it") {
    ShellFixture f;
    f.editor.insert_characters("one\ntwo", 7);
    f.editor.move_to_start();
    f.editor.move_to_line_end();            // end of "one", on the \n
    CHECK(f.editor.kill_to_line_end());     // sitting on it, so it IS the kill
    CHECK(f.line() == "onetwo");            // ...and the next line closes up
    CHECK(f.editor.kill_to_line_end());     // now the rest of the joined line
    CHECK(f.line() == "one");
}

TEST_CASE("kill ring: Ctrl-W cuts the word before the cursor") {
    ShellFixture f;
    f.editor.insert_characters("set cli_setpoint 21", 19);
    f.editor.move_word_left();              // before "21"
    CHECK(f.editor.kill_word_back());       // takes "cli_setpoint "
    CHECK(f.line() == "set 21");
    CHECK(f.editor.yank());
    CHECK(f.line() == "set cli_setpoint 21");
}

TEST_CASE("kill ring: Ctrl-W over trailing space takes the space and the word") {
    ShellFixture f;
    f.editor.insert_characters("alpha beta   ", 13);
    CHECK(f.editor.kill_word_back());
    CHECK(f.line() == "alpha ");
}

TEST_CASE("kill ring: a kill longer than the ring leaves the overflow in place") {
    ShellFixture f;
    const uint16_t n = (uint16_t)(Config::KILL_RING_SIZE + 10);
    std::string big(n, 'x');
    f.editor.insert_characters(big.data(), n);
    f.editor.move_to_line_start();
    CHECK(f.editor.kill_to_line_end());
    // Only what the ring could hold went, so the yank restores exactly that.
    CHECK(f.line() == std::string(10, 'x'));
    CHECK(f.editor.yank());
    CHECK(f.line() == big);
}

// ---------------------------------------------------------------------------
// Mark and region (Ctrl-Space anchors, Ctrl-W / Alt-W act on the span).
// ---------------------------------------------------------------------------

TEST_CASE("region: Ctrl-W kills the span between the mark and the cursor") {
    ShellFixture f;
    f.editor.insert_characters("set cli_setpoint 21", 19);
    f.editor.move_to_line_start();
    for (int i = 0; i < 4; ++i) f.editor.move_right();   // before "cli_"
    f.editor.set_mark();
    f.editor.move_word_right();                          // over "cli_setpoint "
    CHECK(f.editor.mark_active());
    CHECK(f.editor.kill_region());
    CHECK(f.line() == "set 21");
    CHECK_FALSE(f.editor.mark_active());                 // the region is gone with it
    f.editor.move_to_line_start();
    CHECK(f.editor.yank());
    CHECK(f.line() == "cli_setpoint set 21");
}

TEST_CASE("region: the mark may sit on either side of the cursor") {
    ShellFixture f;
    f.editor.insert_characters("alpha beta gamma", 16);
    f.editor.set_mark();                                 // anchor at the END
    for (int i = 0; i < 6; ++i) f.editor.move_left();    // cursor before "gamma"
    uint16_t from = 0, to = 0;
    REQUIRE(f.editor.region(from, to));
    CHECK(from == 10);                                   // ordered, whichever way
    CHECK(to == 16);
    CHECK(f.editor.kill_region());
    CHECK(f.line() == "alpha beta");
}

TEST_CASE("region: an edit drops the mark, so Ctrl-W is a word kill again") {
    ShellFixture f;
    f.editor.insert_characters("alpha beta", 10);
    f.editor.move_to_line_start();
    f.editor.set_mark();
    f.editor.move_to_line_end();
    f.editor.insert_characters("!", 1);                  // ...and there it goes
    CHECK_FALSE(f.editor.mark_active());
    CHECK(f.editor.kill_word_back());                    // what the page falls back to
    CHECK(f.line() == "alpha ");
}

TEST_CASE("region: Alt-W copies without removing, and releases the mark") {
    ShellFixture f;
    f.editor.insert_characters("pump B runs hot", 15);
    for (int i = 0; i < 3; ++i) f.editor.move_left();
    f.editor.set_mark();
    f.editor.move_to_line_end();
    CHECK(f.editor.copy_region());
    CHECK(f.line() == "pump B runs hot");                // untouched
    CHECK_FALSE(f.editor.mark_active());
    f.editor.move_to_line_start();
    CHECK(f.editor.yank());
    CHECK(f.line() == "hotpump B runs hot");
}

TEST_CASE("region: a span across a newline takes the newline with it") {
    ShellFixture f;
    f.editor.insert_characters("one\ntwo", 7);
    f.editor.move_to_start();
    for (int i = 0; i < 2; ++i) f.editor.move_right();   // after "on"
    f.editor.set_mark();
    f.editor.move_to_end();
    CHECK(f.editor.kill_region());
    CHECK(f.line() == "on");
    CHECK(f.editor.yank());
    CHECK(f.line() == "one\ntwo");
}

// ---------------------------------------------------------------------------
// The variable commands, driven against a real bank so the output is real.
// ---------------------------------------------------------------------------

namespace {

    const Vars::Variable CMD_VARS[] = {
        MI("ext_temp", T_F32, 5, 3, 18, 2, SF(-40.0f), SF(85.0f), SF(20.5f), 0, 1,
                       "C", "outside", ""),
        G ("setpoint", T_F32, SF(18.0f), 1, "C", "target temp", ""),
    };

    /** @brief A shell with a bank behind it and its output kept. */
    struct CmdFixture {
        botanik_test::PipeSerial wire;
        uint64_t                       now = 0;
        Terminal::Console              console;
        Terminal::EditorBuffers<Config::SHELL_INPUT_LEN,
                                Config::EDITOR_MAX_ROWS> bufs;
        Terminal::TextEditor           editor;
        CapturingWriter                writer;
        Vars::Value                    values[2]{};
        uint16_t                       index[2]{};
        Vars::Bank                     bank;
        // Declared before shell: its address must be valid in shell's init list.
        SystemContext                  ctx{};
        Shell::CommandShell            shell;

        CmdFixture()
            : console(Botanik::SetupContext(), wire.serial, 80, 24, &now, 115200),
              editor(&console, bufs.storage()),
              shell(console.terminal(), editor, writer, &ctx) {
            editor.reset();
            bank.register_vars(CMD_VARS, 2);
            bank.freeze();
            ctx.vars = &bank;
        }

        void run(const char* cmd) {
            writer.text.clear();
            shell.run(cmd, (uint16_t)std::strlen(cmd));
        }
    };

}

TEST_CASE("shell overrides: a bare name forces a GOOD reading, and `clear all` releases them") {
    // A bare `overrides set` must force a GOOD reading, not carry through
    // whatever quality the row had - only `invalid` asks for a dead sensor.
    CmdFixture f;
    REQUIRE_FALSE(f.bank.var("ext_temp").valid());   // nothing has published it

    f.run("overrides set ext_temp");
    CHECK(f.bank.var("ext_temp").valid());
    f.run("overrides set ext_temp invalid");
    CHECK_FALSE(f.bank.var("ext_temp").valid());

    f.run("overrides set setpoint 5");
    REQUIRE(f.bank.override_count() == 2);
    // Live, and said to be unsaved: the two are different facts now.
    CHECK(f.writer.has("unsaved"));
    f.run("overrides list");
    CHECK(f.writer.has("2 override(s) active, 2 unsaved"));
    // Only `all` releases the lot: a bare verb is a usage error, since it is
    // too easy to type when one row was meant.
    f.run("overrides clear");
    CHECK(f.writer.has("usage:"));
    CHECK(f.bank.override_count() == 2);

    f.run("overrides clear all");
    CHECK(f.writer.has("cleared 2"));
    CHECK(f.bank.override_count() == 0);
}

TEST_CASE("shell overrides: one row can be released by name") {
    CmdFixture f;
    f.run("overrides set ext_temp 5");
    f.run("overrides set setpoint 5");
    REQUIRE(f.bank.override_count() == 2);
    f.run("overrides clear ext_temp");
    CHECK(f.writer.has("ext_temp: override cleared"));
    CHECK(f.bank.override_count() == 1);
    // The older spelling still reads naturally next to a value.
    f.run("overrides set setpoint off");
    CHECK(f.bank.override_count() == 0);
}

TEST_CASE("shell vars get: the detail view, and what it adds when a row is forced") {
    CmdFixture f;
    f.bank.var("ext_temp").publish(Vars::Scalar::F(21.0f), 1000);

    f.run("vars get ext_temp");
    MESSAGE("vars get ext_temp ->\n" << f.writer.text);
    CHECK(f.writer.has("ext_temp = 21.0"));
    CHECK(f.writer.has(": MI"));                  // what kind of row...
    CHECK(f.writer.has("(input, direct)"));       // ...spelled out
    CHECK(f.writer.has("Group"));
    CHECK(f.writer.has(": read-only"));           // and whether `set` takes it
    CHECK(f.writer.has("Raw value"));
    CHECK(f.writer.has("21.0 C   (valid)"));
    CHECK(f.writer.has("-40.0 .. 85.0"));         // the declared band
    CHECK(f.writer.has("Modbus dev 5"));          // where it comes from
    CHECK(f.writer.has("Override value ... : -"));   // nothing forced

    // Forced: both values kept apart, each with its own validity.
    f.run("overrides set ext_temp 35");
    f.run("vars get ext_temp");
    MESSAGE("vars get ext_temp (forced) ->\n" << f.writer.text);
    CHECK(f.writer.has("Raw value ........ : 21.0 C   (valid)"));
    CHECK(f.writer.has("Override value ... : 35.0 C   (valid, unsaved)"));

    // ...and forcing the QUALITY shows on the value it belongs to.
    f.run("overrides set ext_temp 35 invalid");
    f.run("vars get ext_temp");
    CHECK(f.writer.has("Override value ... : 35.0 C   (invalid, unsaved)"));
}

}  // namespace shell

// --------------------------------------------------------------------------
// console_config
// --------------------------------------------------------------------------

namespace console_config {

using namespace Botanik;

namespace {

    /** @brief A Console over the site's real bank, registered in a real task
     *  table - the shape BotanikFirmware builds, minus the pages.
     *
     *  The site's own rows, not a hand-written fixture: `term_cols` / `term_rows`
     *  / `term_serial` are the library's OWN parameters, and the point of the
     *  cases below is that the console and the table agree about them. */
    struct ConfigFixture {
        botanik_test::PipeSerial wire;
        uint64_t             now = 0;
        Vars::Bank           bank;
        Tasks::Registry      reg;
        Terminal::Console    console;

        ConfigFixture()
            : console(Botanik::SetupContext(reg), wire.serial, Config::CONSOLE_COLS,
                      Config::CONSOLE_ROWS, &now, 115200, nullptr, nullptr,
                      Terminal::Ansi::CONFIGURED_TERMINAL, &bank) {
            TestSite::build(bank);
            console.register_tasks();
        }

        /** @brief Run @p n beats of PHASE_CONFIG, the phase the console's
         *  `console_cfg` task sits in. Whole GROUP_SLOW periods, so its rate
         *  group comes due. */
        void beats(int n) {
            for (int i = 0; i < n; i++) {
                now += Config::GROUP_SLOW_MS;
                reg.run_phase(Tasks::PHASE_CONFIG, now);
            }
        }

        Vars::SetResult set(const char* name, const char* value) {
            return bank.set(bank.var(name), value);
        }
        uint32_t serial() const {
            return (uint32_t)Vars::get_int(&bank, "term_serial", 0);
        }
    };

}  // namespace

TEST_CASE("console: the stored geometry lands once the parameters are loaded") {
    ConfigFixture f;

    // Bring-up, in BotanikFirmware::setup()'s order: the console applies what
    // the bank holds BEFORE the parameter load, which is one of the one-time
    // inits Tasks::Registry::setup() runs afterwards. All it can see here are
    // the table's defaults.
    f.console.apply_stored_config();
    REQUIRE(f.console.terminal().cols() == Config::CONSOLE_COLS);
    const uint32_t committed = f.serial();

    // Now the load lands: the saved geometry appears in the bank, and the
    // commit serial comes back UNCHANGED - it was saved with the rest. That is
    // the trap: a console that recorded the serial as applied above would never
    // look again, and a geometry saved on the last run would never reach the
    // screen.
    REQUIRE(f.set("term_cols", "60") == Vars::SET_OK);
    REQUIRE(f.set("term_rows", "20") == Vars::SET_OK);
    REQUIRE(f.serial() == committed);

    f.beats(4);
    CHECK(f.console.terminal().cols() == 60);
    CHECK(f.console.terminal().rows_total() == 20);
    // No alarm model wired, so no reserved bottom row: pages get the lot.
    CHECK(f.console.rows() == 20);
}

TEST_CASE("console: a `term_` edit stages until the group's commit serial moves") {
    ConfigFixture f;
    f.console.apply_stored_config();
    f.beats(4);                       // the bring-up adoption pass, above
    REQUIRE(f.console.terminal().cols() == Config::CONSOLE_COLS);

    // The three rows describe ONE terminal, so an edit waits for the commit
    // rather than walking the screen through geometries that match nothing.
    REQUIRE(f.set("term_cols", "60") == Vars::SET_OK);
    f.beats(4);
    CHECK(f.console.terminal().cols() == Config::CONSOLE_COLS);

    char bumped[8];
    snprintf(bumped, sizeof(bumped), "%u", (unsigned)(f.serial() + 1u));
    REQUIRE(f.set("term_serial", bumped) == Vars::SET_OK);
    f.beats(4);
    CHECK(f.console.terminal().cols() == 60);
}

}  // namespace console_config

// --------------------------------------------------------------------------
// scroll_table
// --------------------------------------------------------------------------

namespace scroll_table {

using namespace Botanik;

namespace {
    /** @brief A table page that is only geometry: three columns, no data. */
    struct FitPage : public Pages::ScrollTablePage {
        explicit FitPage(Terminal::Console& c) : ScrollTablePage(c) {}

        // Page contract: nothing here draws.
        const char* name() const override { return "FIT"; }
        void handle_text(char) override {}
        void handle_command(Command, uint16_t) override {}
        void deactivate() override {}
        void focus() override {}
        void tick(bool) override {}

        uint16_t _data_top()  const override { return 4; }
        uint16_t _window()    const override { return 8; }
        uint16_t _row_count() const override { return 0; }

        uint8_t _columns(Col* out) const override {
            out[0] = Col{ "A",    4  };
            out[1] = Col{ "FLEX", 0  };          // the base fits it
            out[2] = Col{ "C",    10 };
            return 3;
        }
        uint8_t _flex_col() const override { return 1; }

        /** @brief What a drawn row measures. */
        uint16_t row_width() const {
            char line[Config::TABLE_LINE_LEN];
            const char* cells[3] = {"", "", ""};
            return _build_row(line, sizeof line, cells);
        }
        uint16_t width_of(uint8_t i) const {
            return (uint16_t)(_cell_end(i) - _cell_col(i));
        }
    };
}

// The column widths are the display's, read at draw time: a resize is a
// re-layout, and nothing the page cached from the geometry it was built at.
TEST_CASE("scroll table: a resize re-lays the columns out, both ways") {
    botanik_test::PipeSerial wire;
    uint64_t now = 0;
    Terminal::Console console(Botanik::SetupContext(), wire.serial, 80, 24, &now, 4000000u);
    FitPage page(console);

    // MARGIN each side, so the table ends where _draw_separator() does.
    REQUIRE(page.row_width() == 78);
    const uint16_t flex80 = page.width_of(1);

    INFO("widened: the slack goes to the flexible column, the fixed ones stay");
    REQUIRE(console.reconfigure(console.terminal().terminal_type(), 120, 24));
    CHECK(page.row_width()  == 118);
    CHECK(page.width_of(1)  == (uint16_t)(flex80 + 40));
    CHECK(page.width_of(0)  == 4);
    CHECK(page.width_of(2)  == 10);

    INFO("narrowed past what the fixed columns cost: the widest are shaved so"
         " the row still fits, rather than being clipped mid-cell");
    REQUIRE(console.reconfigure(console.terminal().terminal_type(), 30, 24));
    CHECK(page.row_width() == 28);
    CHECK(page.width_of(2) < 10);
    CHECK(page.width_of(0) == 4);       // narrowest cell: nothing to take
}

// A screen too small for even the shaved row must not wrap the arithmetic.
TEST_CASE("scroll table: a display narrower than the columns clamps, not wraps") {
    botanik_test::PipeSerial wire;
    uint64_t now = 0;
    Terminal::Console console(Botanik::SetupContext(), wire.serial, 80, 24, &now, 4000000u);
    FitPage page(console);

    REQUIRE(console.reconfigure(console.terminal().terminal_type(), 8, 24));
    CHECK(page.row_width() > 0);
    CHECK(page.row_width() < Config::TABLE_LINE_LEN);
    CHECK(page.width_of(1) >= 3);       // MIN_COL_W: nothing is shaved away
}

}  // namespace scroll_table
