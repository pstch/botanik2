/**
 * @file test_editor_bytes.cpp
 * @brief TextEditor wire cost: bytes actually emitted per edit.
 *
 * The editor is the one renderer whose reconcile still reasons about the screen
 * itself, because a row-count change needs an IL/DL the cell diff cannot deduce.
 * These tests pin what that costs in bytes, so the reconcile can be simplified
 * without silently getting chattier: each drives an edit against a pipe-backed
 * console, presents, and asserts the drained byte count.
 *
 * The budgets are upper bounds with a little slack, not exact counts - they are
 * meant to catch a regression in kind (a full-screen repaint where a one-row
 * splice belongs), not to freeze the escape-code tuning.
 */
#include <cstdint>                      // uint16_t/uint32_t/uint64_t
#include <cstdio>                       // snprintf
#include <cstring>                      // strlen
#include <fcntl.h>                      // fcntl / O_NONBLOCK
#include <string>                       // std::string
#include <unistd.h>                     // pipe / read / close

#include "libbotanik/console/console.h" // Console
#include "libbotanik/console/editor.h"  // Terminal::TextEditor

#include "doctest.h"                    // the test framework (vendored single header)

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
