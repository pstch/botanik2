/**
 * @file test_editor_wire.cpp
 * @brief What the OPERATOR saw, and what it cost to show them.
 *
 * test_editor.cpp asserts about FRONT, which answers whether the editor's model
 * of the screen is right. Neither of the questions here is that one: an
 * independent VT replays the escapes and must land on the same screen, and the
 * byte count of each edit is committed so a reconciliation change cannot get
 * cheaper-looking by getting wronger, or quietly get dearer.
 *
 * The scenarios are one table, run by both. Add a row and it joins both checks.
 */

#include <string>
#include <vector>

#include "doctest.h"

#include "libbotanik.h"

#include "editor_fixture.h"
#include "libbotanik/utils/text.h"

namespace editor_wire {

using botanik_test::EditorFixture;

static void type(EditorFixture& f, const char* s) {
    f.editor.insert_characters(s, (uint16_t)strlen(s));
}
static void fill(EditorFixture& f, char c, uint16_t n) {
    const std::string s((size_t)n, c);
    f.editor.insert_characters(s.data(), n);
}

/** @brief One editing situation: how to reach it, and the one action whose
 *  cost is measured. Nothing in `reach` is billed. */
struct Scenario {
    const char* name;
    bool        notes;          /**< NOTES prompts: three of the same WIDTH. */
    void      (*reach)(EditorFixture&);
    void      (*act)(EditorFixture&);
    uint16_t    bytes;          /**< Wire cost of `act` as committed. A CEILING:
                                 *   going under it is the point, going over it
                                 *   is the regression this table exists for. */
};

static const Scenario SCENARIOS[] = {
    { "insert on an empty line", false,
      [](EditorFixture&) {},
      [](EditorFixture& f) { type(f, "x"); }, 11 },

    { "insert at the head of a filled row", false,
      [](EditorFixture& f) { fill(f, 'x', 60); f.editor.move_to_line_start(); },
      [](EditorFixture& f) { type(f, "Z"); }, 19 },

    { "insert at the head of a 2-row wrap", false,
      [](EditorFixture& f) { fill(f, 'x', 100); f.editor.move_to_line_start(); },
      [](EditorFixture& f) { type(f, "Z"); }, 23 },

    { "insert at the head of a 3-row wrap", false,
      [](EditorFixture& f) { fill(f, 'x', 180); f.editor.move_to_line_start(); },
      [](EditorFixture& f) { type(f, "Z"); }, 23 },

    { "hard break at the head of a 3-row input", false,
      [](EditorFixture& f) { fill(f, 'x', 180); f.editor.move_to_line_start(); },
      [](EditorFixture& f) { type(f, "\n"); }, 199 },

    { "append past the end of a 2-row wrap", false,
      [](EditorFixture& f) { fill(f, 'x', 100); },
      [](EditorFixture& f) { type(f, "Z"); }, 11 },

    { "backspace at the head of a 2-row wrap", false,
      [](EditorFixture& f) {
          fill(f, 'x', 100);
          f.editor.move_to_line_start();
          f.editor.move_right();
      },
      [](EditorFixture& f) { f.editor.backspace(1); }, 11 },

    { "delete_forward inside a 3-row wrap", false,
      [](EditorFixture& f) {
          fill(f, 'x', 180);
          f.editor.move_to_line_start();
          for (int i = 0; i < 5; i++) f.editor.move_right();
      },
      [](EditorFixture& f) { f.editor.delete_forward(1); }, 11 },

    { "kill_word_back mid-line", false,
      [](EditorFixture& f) { type(f, "the quick brown fox jumps"); },
      [](EditorFixture& f) { (void)f.editor.kill_word_back(); }, 15 },

    { "replace_all onto one row", false,
      [](EditorFixture& f) { fill(f, 'x', 100); },
      [](EditorFixture& f) { f.editor.replace_all("clear", 5); }, 38 },

    { "replace_all onto two rows", false,
      [](EditorFixture& f) { type(f, "hi"); },
      [](EditorFixture& f) {
          const std::string s(100, 'y');
          f.editor.replace_all(s.data(), 100);
      }, 144 },

    { "region raised over a word", false,
      [](EditorFixture& f) {
          type(f, "the quick brown fox");
          f.editor.move_to_line_start();
      },
      [](EditorFixture& f) {
          f.editor.set_mark();
          for (int i = 0; i < 10; i++) f.editor.move_right();
      }, 24 },

    { "NOTES hard break at the head of a 3-row input", true,
      [](EditorFixture& f) { fill(f, 'A', 180); f.editor.move_to_line_start(); },
      [](EditorFixture& f) { type(f, "\n"); }, 26 },

    { "NOTES split at a wrap boundary", true,
      [](EditorFixture& f) {
          fill(f, 'A', 100);
          for (int i = 0; i < 22; i++) f.editor.move_left();
      },
      [](EditorFixture& f) { type(f, "\n"); }, 7 },

    { "NOTES join of two lines", true,
      [](EditorFixture& f) {
          type(f, "first\nsecond");
          f.editor.move_to_start();
          f.editor.move_down();
      },
      [](EditorFixture& f) { f.editor.backspace(1); }, 28 },
};

static constexpr size_t N_SCENARIOS = sizeof(SCENARIOS) / sizeof(SCENARIOS[0]);

/** @brief Run @p s up to its measured action and return that action's bytes,
 *  leaving @p f on the screen the action produced. */
static size_t run(EditorFixture& f, const Scenario& s) {
    s.reach(f);
    (void)f.repaint_from_scratch();
    f.drain();
    s.act(f);
    return f.pump().size();
}

TEST_CASE("editor: the wire puts the screen where FRONT asked for it") {
    for (size_t i = 0; i < N_SCENARIOS; i++) {
        const Scenario& s = SCENARIOS[i];
        EditorFixture f(24, 80, s.notes);
        (void)run(f, s);
        f.check_wire(s.name);
        // FRONT still has to be right too: the VT agreeing with a FRONT that
        // drifted from the buffer would be two screens wrong in step.
        botanik_test::check_converged(f, s.name);
    }
}

TEST_CASE("editor: an edit costs the bytes it is budgeted") {
    std::string ledger;
    bool moved = false;
    char line[128];

    for (size_t i = 0; i < N_SCENARIOS; i++) {
        const Scenario& s = SCENARIOS[i];
        EditorFixture f(24, 80, s.notes);
        const size_t got = run(f, s);
        // What the same screen costs painted from nothing. Not asserted - it
        // is the reference a moved number is read against: an incremental path
        // drifting towards it is the path being given up.
        const size_t repaint = f.repaint_from_scratch().size();
        if (got != s.bytes) moved = true;
        snprintf(line, sizeof line, "  %5u  (ceiling %5u)  repaint %5u   %s\n",
                 (unsigned)got, (unsigned)s.bytes, (unsigned)repaint, s.name);
        ledger += line;
        INFO("scenario: " << s.name);
        CHECK((unsigned)got <= (unsigned)s.bytes);
    }

    // Cheaper is the win, and it still gets printed: a ceiling nobody lowers
    // stops measuring anything. Lower it in the same commit that earned it.
    if (moved) MESSAGE("editor wire-byte ledger:\n" << ledger);
}

}  // namespace editor_wire
