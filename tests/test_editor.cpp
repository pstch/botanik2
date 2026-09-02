/**
 * @file test_editor.cpp
 * @brief The one invariant the editor's byte-efficiency rests on: whatever the
 *        incremental path emits, the screen must end up where a full repaint
 *        would have put it.
 *
 * TextEditor reconciles its own rows rather than going through the back-buffer
 * diff (25 bytes against 195 - see docs/dev/editor.md), which buys the wire
 * budget and costs the one thing a diff gives away free: the screen can drift
 * from the model and nothing notices. These cases are that notice.
 */

#include <string>
#include <vector>

#include "doctest.h"

#include "libbotanik.h"

#include "term_fixture.h"

namespace editor_invariant {

using namespace Botanik;

/** @brief A console with one editor on it and nothing else, so a full repaint
 *  of the editor is a full repaint of everything on screen. */
struct EditorFixture {
    botanik_test::PipeSerial     wire;
    uint64_t                     now = 0;
    Terminal::Console            console;
    Terminal::EditorBuffers<2048, 16> store;
    Terminal::TextEditor         editor;

    explicit EditorFixture(uint16_t rows = 24, uint16_t cols = 80,
                           bool notes_prompts = false)
        : console(Botanik::SetupContext(), wire.serial, &now),
          editor(&console, store.storage()) {
        console.reconfigure(Terminal::ConsoleConfig().type, cols, rows);
        // High enough that the cooperative TX floor never throttles a paint,
        // so what the grid holds is never merely "not sent yet".
        console.terminal().set_baud(4000000u);
        if (notes_prompts) {
            // NOTES: the three prompts are the same WIDTH, which is what
            // docs/dev/editor.md requires and what makes a class flip
            // invisible to a length comparison.
            editor.set_ps1("> ", 2);
            editor.set_ps2("  ", 2);
            editor.set_ps3("> ", 2);
        } else {
            editor.set_ps1("botanik> ", 9);
            editor.set_ps2("  ", 2);
            editor.set_ps3("> ", 5);
        }
        editor.reset();
        drain();
    }

    void drain() { (void)wire.capture(); }

    /** @brief Every row of the FRONT grid, right-trimmed. */
    std::vector<std::string> screen() {
        std::vector<std::string> out;
        char row[160];
        for (uint16_t r = 0; r < console.terminal().rows_total(); r++) {
            const uint16_t n = console.terminal().front_row(r, row, sizeof row);
            std::string s(row, n);
            while (!s.empty() && s.back() == ' ') s.pop_back();
            out.push_back(s);
        }
        return out;
    }

    /** @brief What the screen WOULD hold if it were painted from the model,
     *  with the editor's own state left exactly as it was.
     *
     *  Ctrl+L on the real console; here the same two calls it makes. */
    std::vector<std::string> repainted() {
        console.terminal().establish_screen();
        editor.redraw();
        return screen();
    }
};

/** @brief Assert the incremental screen equals the repainted one, naming the
 *  first row that differs. */
void check_converged(EditorFixture& f, const char* what) {
    const std::vector<std::string> incremental = f.screen();
    const std::vector<std::string> full        = f.repainted();
    REQUIRE(incremental.size() == full.size());
    for (size_t r = 0; r < full.size(); r++) {
        INFO(what << ": row " << r
             << "\n  incremental: '" << incremental[r] << "'"
             << "\n  full redraw: '" << full[r] << "'");
        CHECK(incremental[r] == full[r]);
    }
}

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
    f.editor.backspace(50);
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

}  // namespace editor_invariant
