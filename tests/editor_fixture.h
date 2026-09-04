#ifndef BOTANIK_TEST_EDITOR_FIXTURE_H
#define BOTANIK_TEST_EDITOR_FIXTURE_H

/**
 * @file editor_fixture.h
 * @brief A console with one editor on it, and the three ways to assert about
 *        what it drew.
 *
 * The editor is not a Page: it owns its band, repaints through redraw() of its
 * own, and until present() runs it has emitted nothing at all. So it needs its
 * own fixture, and it is the renderer that most needs check_wire() - a
 * convergence check reads FRONT, which a renderer that reconciles by hand can
 * satisfy while the escapes it emitted say something else.
 */

#include <cstring>
#include <string>
#include <vector>

#include "doctest.h"

#include "libbotanik.h"

#include "page_fixture.h"

namespace botanik_test {

    /** @brief A console with one editor on it and nothing else, so a full
     *  repaint of the editor is a full repaint of everything on screen. */
    struct EditorFixture : PageFixture {
        Botanik::Terminal::EditorBuffers<2048, 16> store;
        Botanik::Terminal::TextEditor              editor;

        explicit EditorFixture(uint16_t rows = 24, uint16_t cols = 80,
                               bool notes_prompts = false)
            : PageFixture(rows, cols),
              editor(&console, store.storage()) {
            // sizeof - 1 rather than a written-out length, the way the two real
            // callers do it: a prompt and a length that disagree is a memcpy
            // off the end of a literal, and it reads as correct.
            if (notes_prompts) {
                // NOTES: the three prompts are the same WIDTH, which is what
                // docs/dev/editor.md requires and what makes a class flip
                // invisible to a length comparison. @see NotesPage::PROMPT_LINE
                set_ps("> ", "  ", "> ");
            } else {
                set_ps("botanik> ", "> ", "...> ");   // @see ShellPage::_setup_editor
            }
            // Blank BEFORE the first paint: reset() stamps the prompt into
            // FRONT, and a blank that lands after it leaves the editor
            // believing a prompt is on screen that nothing will repaint.
            reset_wire();
            editor.reset();
            (void)pump();
        }

        void set_ps(const char* p1, const char* p2, const char* p3) {
            editor.set_ps1(p1, (uint16_t)strlen(p1));
            editor.set_ps2(p2, (uint16_t)strlen(p2));
            editor.set_ps3(p3, (uint16_t)strlen(p3));
        }

        /** @brief Blank everything and repaint the editor from its model, so a
         *  following action is measured against a screen that is already right.
         *  @return the wire bytes that repaint cost. */
        std::string repaint_from_scratch() {
            reset_wire();
            editor.redraw();
            return pump();
        }

        /** @brief What the screen WOULD hold painted from the model, with the
         *  editor's own state left exactly as it was. Ctrl+L on the real
         *  console; here the same two calls it makes. */
        std::vector<std::string> repainted() {
            console.terminal().establish_screen();
            editor.redraw();
            (void)pump();          // keep the VT tracking what the wire got
            return screen();
        }
    };

    /** @brief FRONT after the incremental path equals FRONT after a repaint.
     *  Not PageFixture::converged(): a TextEditor is not a Page. */
    inline void check_converged(EditorFixture& f, const char* what) {
        const std::vector<std::string> incremental = f.screen();
        PageFixture::same(incremental, f.repainted(), what);
    }

}  // namespace botanik_test

#endif // BOTANIK_TEST_EDITOR_FIXTURE_H
