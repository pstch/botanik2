/**
 * @file test_editor_wire.cpp
 * @brief What the operator saw, and what it cost to show them.
 *
 * test_editor.cpp asserts about FRONT, which answers whether the editor's model
 * of the screen is right. Neither of the questions here is that one: an
 * independent VT replays the escapes and must land on the same screen, and the
 * byte count of each edit is committed so a reconciliation change cannot get
 * cheaper-looking by getting wronger, or quietly get dearer.
 *
 * The scenarios are one table, run by both. Add a row and it joins both checks.
 * `BOTANIK_LEDGER=1` prints the whole ledger whether or not a number moved.
 */

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "doctest.h"

#include "libbotanik.h"

#include "editor_fixture.h"

namespace tests { namespace editor_wire {

using botanik_test::EditorFixture;

/** @brief Text with no short period: cells at distance d match only when d
 *  is a multiple of 36, so a slide the ledger credits is one that happened.
 *  A uniform fill hid every cross-row reflow, the char pulled up from the row
 *  below being the char it replaced. */
static std::string varied(uint16_t n) {
    static const char AL[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::string s;
    s.reserve(n);
    for (uint16_t i = 0; i < n; i++) s += AL[(i * 11u) % 36u];
    return s;
}



static void type(EditorFixture& f, const char* s) {
    f.editor.insert_characters(s, (uint16_t)strlen(s));
}
static void fill(EditorFixture& f, uint16_t n) {
    const std::string s = varied(n);
    f.editor.insert_characters(s.data(), n);
}
using botanik_test::seek;
/** @brief @p n hard-broken lines, ten characters each. */
static void lines(EditorFixture& f, uint16_t n) {
    for (uint16_t i = 0; i < n; i++) { fill(f, 10); type(f, "\n"); }
}

/** @brief Whose editor: the shell's bottom-anchored prompt, or the NOTES
 *  page's top-anchored viewport with its same-width prompts. */
enum Geometry : uint8_t { SHELL, NOTES };

/** @brief One editing situation: how to reach it, and the one action whose
 *  cost is measured. Nothing in `reach` is billed. */
struct Scenario {
    const char* name;
    Geometry    geo;
    void      (*reach)(EditorFixture&);
    void      (*act)(EditorFixture&);
    uint16_t    bytes;          /**< Wire cost of `act` as committed. A CEILING:
                                 *   going under it is the point, going over it
                                 *   is the regression this table exists for. */
};

static const Scenario SCENARIOS[] = {
    { "insert on an empty line", SHELL,
      [](EditorFixture&) {},
      [](EditorFixture& f) { type(f, "x"); }, 1 },

    { "insert at the head of a filled row", SHELL,
      [](EditorFixture& f) { fill(f, 60); f.editor.move_to_line_start(); },
      [](EditorFixture& f) { type(f, "Z"); }, 9 },

    { "insert at the head of a 2-row wrap", SHELL,
      [](EditorFixture& f) { fill(f, 100); f.editor.move_to_line_start(); },
      [](EditorFixture& f) { type(f, "Z"); }, 25 },

    // The last row keeps 8 characters: too few to have looked worth a slide.
    { "insert at the head of a wrap with a short last row", SHELL,
      [](EditorFixture& f) { fill(f, 71 + 78 + 8); f.editor.move_to_line_start(); },
      [](EditorFixture& f) { type(f, "Z"); }, 33 },

    { "insert at the head of a 3-row wrap", SHELL,
      [](EditorFixture& f) { fill(f, 180); f.editor.move_to_line_start(); },
      [](EditorFixture& f) { type(f, "Z"); }, 33 },

    { "insert mid a 3-row wrap", SHELL,
      [](EditorFixture& f) { fill(f, 180); seek(f, 40); },
      [](EditorFixture& f) { type(f, "Z"); }, 33 },

    { "append past the end of a 2-row wrap", SHELL,
      [](EditorFixture& f) { fill(f, 100); },
      [](EditorFixture& f) { type(f, "Z"); }, 1 },

    { "backspace at the head of a 2-row wrap", SHELL,
      [](EditorFixture& f) { fill(f, 100); seek(f, 1); },
      [](EditorFixture& f) { f.editor.backspace(1); }, 30 },

    { "backspace mid a 3-row wrap", SHELL,
      [](EditorFixture& f) { fill(f, 180); seek(f, 40); },
      [](EditorFixture& f) { f.editor.backspace(1); }, 46 },

    { "delete_forward mid a 3-row wrap", SHELL,
      [](EditorFixture& f) { fill(f, 180); seek(f, 5); },
      [](EditorFixture& f) { f.editor.delete_forward(1); }, 43 },

    { "hard break at the head of a 3-row input", SHELL,
      [](EditorFixture& f) { fill(f, 180); f.editor.move_to_line_start(); },
      [](EditorFixture& f) { type(f, "\n"); }, 73 },

    { "hard break mid a 3-row input", SHELL,
      [](EditorFixture& f) { fill(f, 180); seek(f, 40); },
      [](EditorFixture& f) { type(f, "\n"); }, 97 },

    { "join of two lines", SHELL,
      [](EditorFixture& f) {
          fill(f, 30); type(f, "\n"); fill(f, 30);
          f.editor.move_to_start();
          f.editor.move_down();
      },
      [](EditorFixture& f) { f.editor.backspace(1); }, 53 },

    // The joined line is long: its text slides right under the head rather
    // than being written after it.
    { "join of a short line with a wrapped one", SHELL,
      [](EditorFixture& f) {
          fill(f, 10); type(f, "\n"); fill(f, 100);
          f.editor.move_to_start();
          f.editor.move_down();
      },
      [](EditorFixture& f) { f.editor.backspace(1); }, 67 },

    { "kill_to_line_end mid a 3-row wrap", SHELL,
      [](EditorFixture& f) { fill(f, 180); seek(f, 40); },
      [](EditorFixture& f) { (void)f.editor.kill_to_line_end(); }, 24 },

    { "kill_word_back mid-line", SHELL,
      [](EditorFixture& f) { type(f, "the quick brown fox jumps"); },
      [](EditorFixture& f) { (void)f.editor.kill_word_back(); }, 10 },

    { "yank a word mid-line", SHELL,
      [](EditorFixture& f) {
          type(f, "the quick brown fox");
          (void)f.editor.kill_word_back();
          seek(f, 4);
      },
      [](EditorFixture& f) { (void)f.editor.yank(); }, 11 },

    { "replace_all onto one row", SHELL,
      [](EditorFixture& f) { fill(f, 100); },
      [](EditorFixture& f) { f.editor.replace_all("clear", 5); }, 26 },

    // Obeyed, the origin's band carries the 9-cell prompt for a 22-byte pair.
    { "replace_all onto two rows", SHELL,
      [](EditorFixture& f) { type(f, "hi"); },
      [](EditorFixture& f) {
          const std::string s = varied(100);
          f.editor.replace_all(s.data(), 100);
      }, 131 },

    { "region raised over a word", SHELL,
      [](EditorFixture& f) {
          type(f, "the quick brown fox");
          f.editor.move_to_line_start();
      },
      [](EditorFixture& f) {
          f.editor.set_mark();
          for (int i = 0; i < 10; i++) f.editor.move_right();
      }, 28 },

    // Repeated words: sliding right by one changes only the cells around each
    // space, so a rewrite looks cheap until the cursor has to reach them all.
    { "insert at the head of 3 rows of repeated words", SHELL,
      [](EditorFixture& f) {
          for (int i = 0; i < 12; i++) type(f, "aaaaaaaaaaaaaaaaaa ");
          f.editor.move_to_start();
      },
      [](EditorFixture& f) { type(f, "x"); }, 41 },

    // 72 characters wrap one onto a second row; the first backspace brings the
    // text back to exactly the last column, which parks the cursor past it.
    { "backspace twice from one past a full row", SHELL,
      [](EditorFixture& f) { fill(f, 72); f.editor.backspace(1); },
      [](EditorFixture& f) { f.editor.backspace(1); }, 9 },

    { "cursor up a row in a 3-row wrap", SHELL,
      [](EditorFixture& f) { fill(f, 180); },
      [](EditorFixture& f) { f.editor.move_up(); }, 3 },

    { "NOTES insert mid a 3-row line", NOTES,
      [](EditorFixture& f) { fill(f, 180); seek(f, 40); },
      [](EditorFixture& f) { type(f, "Z"); }, 30 },

    // Period-11 text: each row, slid one to the right, lines up with the row
    // above it, which a row slide priced without the sideways slide mistook
    // for a move.
    { "NOTES insert at the head of a periodic 3-row line", NOTES,
      [](EditorFixture& f) {
          for (int i = 0; i < 19; i++) type(f, " 0123456890");
          f.editor.move_to_start();
      },
      [](EditorFixture& f) { type(f, "X"); }, 27 },

    { "NOTES delete_forward mid a 3-row line", NOTES,
      [](EditorFixture& f) { fill(f, 180); seek(f, 40); },
      [](EditorFixture& f) { f.editor.delete_forward(1); }, 40 },

    { "NOTES hard break at the head of a 3-row line", NOTES,
      [](EditorFixture& f) { fill(f, 180); f.editor.move_to_line_start(); },
      [](EditorFixture& f) { type(f, "\n"); }, 25 },

    { "NOTES hard break mid a 3-row line", NOTES,
      [](EditorFixture& f) { fill(f, 180); seek(f, 40); },
      [](EditorFixture& f) { type(f, "\n"); }, 112 },

    { "NOTES split at a wrap boundary", NOTES,
      [](EditorFixture& f) {
          fill(f, 100);
          for (int i = 0; i < 22; i++) f.editor.move_left();
      },
      [](EditorFixture& f) { type(f, "\n"); }, 5 },

    { "NOTES join of two lines", NOTES,
      [](EditorFixture& f) {
          type(f, "first\nsecond");
          f.editor.move_to_start();
          f.editor.move_down();
      },
      [](EditorFixture& f) { f.editor.backspace(1); }, 23 },

    { "NOTES join of a short line with a wrapped one", NOTES,
      [](EditorFixture& f) {
          fill(f, 10); type(f, "\n"); fill(f, 100);
          f.editor.move_to_start();
          f.editor.move_down();
      },
      [](EditorFixture& f) { f.editor.backspace(1); }, 60 },

    // 31 rows of text over a 22-row viewport: the cursor leaves the window by
    // one row in each direction, and the band should slide rather than repaint.
    { "NOTES viewport slides down a row", NOTES,
      [](EditorFixture& f) {
          lines(f, 30);
          for (int i = 0; i < 21; i++) f.editor.move_up();
      },
      [](EditorFixture& f) { f.editor.move_up(); }, 35 },

    { "NOTES viewport slides up a row", NOTES,
      [](EditorFixture& f) {
          lines(f, 30);
          f.editor.move_to_start();
          for (int i = 0; i < 21; i++) f.editor.move_down();
      },
      [](EditorFixture& f) { f.editor.move_down(); }, 34 },
};

static constexpr size_t N_SCENARIOS = sizeof(SCENARIOS) / sizeof(SCENARIOS[0]);

static EditorFixture* make(const Scenario& s) {
    return new EditorFixture(24, 80, s.geo == NOTES, s.geo == NOTES);
}

/** @brief Run @p s up to its measured action and return that action's bytes,
 *  leaving @p f on the screen the action produced. `BOTANIK_LEDGER_DUMP=1`
 *  prints them, escapes spelled out, for reading where a count came from. */
static size_t run(EditorFixture& f, const Scenario& s) {
    s.reach(f);
    (void)f.repaint_from_scratch();
    f.drain();
    s.act(f);
    const std::string bytes = f.pump();
    if (getenv("BOTANIK_LEDGER_DUMP") != nullptr)
        MESSAGE(std::string(s.name) << " (" << bytes.size() << " bytes):\n  " << botanik_test::shown(bytes));
    return bytes.size();
}

TEST_CASE("editor: the wire puts the screen where FRONT asked for it") {
    for (size_t i = 0; i < N_SCENARIOS; i++) {
        const Scenario& s = SCENARIOS[i];
        EditorFixture* f = make(s);
        (void)run(*f, s);
        f->check_wire(s.name);
        // FRONT still has to be right too: the VT agreeing with a FRONT that
        // drifted from the buffer would be two screens wrong in step.
        botanik_test::check_converged(*f, s.name);
        delete f;
    }
}

TEST_CASE("editor: an edit costs the bytes it is budgeted") {
    std::string ledger;
    bool moved = false;
    char line[128];

    for (size_t i = 0; i < N_SCENARIOS; i++) {
        const Scenario& s = SCENARIOS[i];
        EditorFixture* f = make(s);
        const size_t got = run(*f, s);
        // What the same screen costs painted from nothing. Not asserted - it
        // is the reference a moved number is read against: an incremental path
        // drifting towards it is the path being given up.
        const size_t repaint = f->repaint_from_scratch().size();
        delete f;
        if (got != s.bytes) moved = true;
        snprintf(line, sizeof line, "  %5u  (ceiling %5u)  repaint %5u   %s\n",
                 (unsigned)got, (unsigned)s.bytes, (unsigned)repaint, s.name);
        ledger += line;
        INFO("scenario: " << s.name);
        CHECK((unsigned)got <= (unsigned)s.bytes);
    }

    // Cheaper is the win, and it still gets printed: a ceiling nobody lowers
    // stops measuring anything. Lower it in the same commit that earned it.
    if (moved || getenv("BOTANIK_LEDGER") != nullptr)
        MESSAGE("editor wire-byte ledger:\n" << ledger);
}

}}  // namespace tests::editor_wire
