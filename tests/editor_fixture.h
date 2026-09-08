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

namespace tests { namespace botanik_test {

    /** @brief A console with one editor on it and nothing else, so a full
     *  repaint of the editor is a full repaint of everything on screen. */
    struct EditorFixture : PageFixture {
        /** @brief Rows sized to the grid, so a top-anchored viewport has text
         *  to scroll over on a full-height screen. */
        Botanik::Terminal::EditorBuffers<2048, Botanik::Config::TERMINAL_MAX_ROWS> store;
        Botanik::Terminal::TextEditor editor;

        /** @param notes_prompts NOTES' three same-width prompts, not the shell's.
         *  @param notes_page   NOTES' geometry too: top-anchored under the
         *         status bar, a viewport leaving the page its bottom row.
         *         @see NotesPage::focus */
        explicit EditorFixture(uint16_t rows = 24, uint16_t cols = 80,
                               bool notes_prompts = false, bool notes_page = false)
            : PageFixture(rows, cols),
              editor(&console, store.storage()) {
            // sizeof - 1 rather than a written-out length, the way the two real
            // callers do it: a prompt and a length that disagree is a memcpy
            // off the end of a literal, and it reads as correct.
            if (notes_prompts) {
                // NOTES: the three prompts are the same width, which is what
                // makes a class flip invisible to a length comparison.
                // @see NotesPage::PROMPT_LINE
                set_ps("> ", "  ", "> ");
            } else {
                set_ps("botanik> ", "> ", "...> ");   // @see ShellPage::_setup_editor
            }
            if (notes_page) {
                editor.set_anchor(Botanik::Terminal::Anchor::TOP);
                editor.set_origin(1);
                editor.set_view_rows((uint16_t)(rows - 2));
            } else {
                editor.set_origin((uint16_t)(rows - 1));   // @see ShellPage::_setup_editor
            }
            // Blank before the first paint: reset() stamps the prompt into
            // FRONT, and a blank that lands after it leaves the editor
            // believing a prompt is on screen that nothing will repaint.
            reset_wire();
            editor.reset();
            show_cursor();
            (void)pump();
        }

        /** @brief Both real pages show the cursor, so the park after every
         *  edit is part of what the link carries; a ledger without it bills
         *  a cursor motion at nothing. After every establish, which hides it,
         *  the way ShellPage::_full_redraw() and NotesPage::focus() do. */
        void show_cursor() { console.terminal().set_cursor_visible(true); }

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
            show_cursor();
            return pump();
        }

        /** @brief What the screen would hold painted from the model, with the
         *  editor's own state left as it was. Ctrl+L on the real console; here
         *  the same two calls it makes. */
        std::vector<std::string> repainted() {
            console.terminal().establish_screen();
            editor.redraw();
            show_cursor();
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

}}  // namespace tests::botanik_test

#endif // BOTANIK_TEST_EDITOR_FIXTURE_H
