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
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>

#include "doctest.h"

#include "libbotanik/console/console.h"
#include "libbotanik/console/editor.h"

using namespace Botanik;

namespace {

    // A TextEditor over a real Console whose serial is a pipe we can drain. Baud
    // is high so present()'s pacing never truncates a frame: we want the total
    // cost of an edit, not what fits in one tick.
    struct EditorBytes {
        int                   fds[2];
        HAL::Serial::Raw      raw;
        Utils::BufferedSerial serial;
        uint32_t              now = 0;
        Terminal::Console     console;
        Terminal::TextEditor  editor;

        EditorBytes()
            : serial(&raw), console(serial, 80, 24, &now, 4000000u), editor(&console) {
            REQUIRE(::pipe(fds) == 0);
            ::fcntl(fds[0], F_SETFL, O_NONBLOCK);
            raw.set_fds(fds[0], fds[1]);
            console.terminal().establish_screen();
            editor.reset();
            flush();                 // discard bring-up + the initial prompt
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

// A soft-wrap growing the editor by a row exercises both shifts at once: the IL
// slides the rows below, and each reflowed row slides one column sideways. The
// budget proves both are being used - repainting the wrapped body outright costs
// several hundred bytes.
TEST_CASE("editor bytes: growing by a wrapped row rides a scroll, not a repaint") {
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
    CHECK(cost <= 90);
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
