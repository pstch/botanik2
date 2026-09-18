/**
 * @file test_editor.cpp
 * @brief The one invariant the editor's byte-efficiency rests on: whatever the
 *        incremental path emits, the screen must end up where a full repaint
 *        would have put it.
 *
 * TextEditor reconciles its own rows rather than going through the back-buffer
 * diff (25 bytes against 195), which buys the wire
 * budget and costs the one thing a diff gives away free: the screen can drift
 * from the model and nothing notices. These cases are that notice.
 */

#include <string>
#include <vector>

#include "doctest.h"

#include "libbotanik.h"

#include "editor_fixture.h"

namespace tests { namespace editor_invariant {

using namespace Botanik;
using botanik_test::EditorFixture;
using botanik_test::check_converged;

TEST_CASE("editor: typing leaves the screen a full repaint would") {
    EditorFixture f;
    for (int i = 0; i < 40; i++) {
        const char c = (char)('a' + (i % 26));
        f.editor.insert_characters(&c, 1);
    }
    check_converged(f, "40 characters");
}

TEST_CASE("editor: a soft wrap leaves the screen a full repaint would") {
    EditorFixture f;
    const std::string text(100, 'x');       // past 80 columns: two rows
    f.editor.insert_characters(text.data(), (uint16_t)text.size());
    REQUIRE(f.editor.occupied_rows() == 2);
    check_converged(f, "a wrapped line");
}

TEST_CASE("editor: backspacing a wrapped line back to one row converges") {
    EditorFixture f;
    const std::string text(100, 'x');
    f.editor.insert_characters(text.data(), (uint16_t)text.size());
    REQUIRE(f.editor.occupied_rows() == 2);
    (void)f.editor.handle_key(Botanik::Terminal::InputEvent::Command::BACKSPACE, 50);
    REQUIRE(f.editor.occupied_rows() == 1);
    check_converged(f, "shrunk back to one row");
}

TEST_CASE("editor: replacing a wrapped line with a shorter one converges") {
    // The shape a shell history recall has: the draft occupies more rows than
    // what replaces it. Done as ONE operation, which is the whole point - three
    // edits inside one suppressed window is what desynchronised the screen from
    // the buffer, and ran a command the operator never saw.
    EditorFixture f;
    const std::string draft(100, 'x');
    f.editor.insert_characters(draft.data(), (uint16_t)draft.size());
    REQUIRE(f.editor.occupied_rows() == 2);

    f.editor.replace_all("clear", 5);
    REQUIRE(f.editor.occupied_rows() == 1);
    CHECK(std::string(f.editor.buffer(), f.editor.buffer_len()) == "clear");
    INFO("the recalled text must be ON SCREEN, not merely in the buffer");
    check_converged(f, "replace_all onto one row");
}

TEST_CASE("editor: replacing a short line with a longer one converges") {
    EditorFixture f;
    f.editor.insert_characters("hi", 2);
    const std::string longer(100, 'y');
    f.editor.replace_all(longer.data(), (uint16_t)longer.size());
    REQUIRE(f.editor.occupied_rows() == 2);
    check_converged(f, "replace_all onto two rows");
}

TEST_CASE("editor: replace_all with an empty line converges") {
    EditorFixture f;
    f.editor.insert_characters("something", 9);
    f.editor.replace_all("", 0);
    CHECK(f.editor.buffer_len() == 0);
    check_converged(f, "replace_all with nothing");
}

TEST_CASE("editor: splitting a wrapped NOTES line converges") {
    // NOTES gives all three prompts the same width, so a row whose prompt CLASS
    // flips (wrap-over -> operator-started) changes no length. A gutter rewrite
    // gated on the length alone can never fire there.
    EditorFixture f(24, 80, /*notes_prompts=*/true);
    const std::string text(100, 'A');
    f.editor.insert_characters(text.data(), (uint16_t)text.size());
    REQUIRE(f.editor.occupied_rows() == 2);
    for (int i = 0; i < 22; i++) (void)f.editor.handle_key(Botanik::Terminal::InputEvent::Command::LEFT);
    f.editor.insert_characters("\n", 1);
    check_converged(f, "ENTER at a wrap boundary");
}

TEST_CASE("editor: joining two NOTES lines converges") {
    EditorFixture f(24, 80, /*notes_prompts=*/true);
    f.editor.insert_characters("first\nsecond", 12);
    REQUIRE(f.editor.occupied_rows() == 2);
    botanik_test::to_start(f.editor);
    (void)f.editor.handle_key(Botanik::Terminal::InputEvent::Command::DOWN);
    (void)f.editor.handle_key(Botanik::Terminal::InputEvent::Command::BACKSPACE, 1);              // remove the newline: one row again
    REQUIRE(f.editor.occupied_rows() == 1);
    check_converged(f, "backspace over a newline");
}


TEST_CASE("editor: a NOTES reflow then a word kill leaves no stray glyph") {
    // The sequence that left a character at the right margin which no later
    // edit cleared - FRONT and BACK agreed, so the diff had nothing to emit and
    // the row stayed wrong until a full repaint.
    EditorFixture f(24, 80, /*notes_prompts=*/true);
    const std::string alpha = std::string("abcdefghijklmnopqrstuvwxyz")
                            + "abcdefghijklmnopqrstuvwxyz";
    f.editor.insert_characters(alpha.data(), (uint16_t)alpha.size());
    (void)f.editor.handle_key(Botanik::Terminal::InputEvent::Command::PAGE_UP);
    f.editor.insert_characters("the quick brown fox ", 20);
    f.editor.insert_characters("0123456789", 10);
    (void)f.editor.handle_key(Botanik::Terminal::InputEvent::Command::PAGE_UP);
    (void)f.editor.handle_key(Botanik::Terminal::InputEvent::Command::PAGE_DOWN);
    f.editor.insert_characters("\n", 1);
    (void)f.editor.handle_key(Botanik::Terminal::InputEvent::Command::UP);
    (void)f.editor.handle_key(Botanik::Terminal::InputEvent::Command::END);
    (void)f.editor.handle_key(Botanik::Terminal::InputEvent::Command::CTRL_W);
    check_converged(f, "reflow then kill_word_back");
}

}}  // namespace tests::editor_invariant
