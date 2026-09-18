/**
 * @file notes.cpp
 * @brief NotesPage implementation - the full-screen note editor.
 */
#include "libbotanik/pages/notes.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_NOTES_PAGE

#include <cstdio>
#include <cstring>

#include "libbotanik/hal/platform.h"

namespace Botanik { namespace Pages {

    void NotesPage::handle_command(Command cmd, uint16_t count) {
        // TAB and CTRL_L never arrive: the console takes them for page
        // switching and redraw before a page is offered the event.
        // Motion is the cursor's here, not history: there is no shell under
        // this page. Cut and paste run over the console-wide ring, so a line
        // cut here yanks at the shell prompt and back. CTRL_U is deliberately
        // unbound: it would clear the note, with nothing to put it back.
        switch (cmd) {
            case Command::ENTER:
            case Command::SHIFT_ENTER: _editor.insert_characters("\n", 1); break;
            case Command::CTRL_O:      _save();                            break;
            case Command::CTRL_U:                                          break;
            default:                   (void)_editor.handle_key(cmd, count); break;
        }
        _settle();
    }

    void NotesPage::focus() {
        uint16_t rows = _console.rows();
        if (rows < 3) return;                 // no room for text and a status row

        _editor.set_anchor(Terminal::Anchor::TOP);
        _editor.set_origin(1);                // row 0 is the status bar
        _editor.set_view_rows((uint16_t)(rows - 2));  // last row is ours
        _editor.set_ps1(PROMPT_LINE(), PROMPT_COLS);  // first row of the note
        _editor.set_ps2(PROMPT_WRAP(), PROMPT_COLS);  // a soft-wrapped tail
        _editor.set_ps3(PROMPT_LINE(), PROMPT_COLS);  // a row the operator started

        if (!_loaded) { _loaded = true; _load(); }

        // The status row is a standing property of that row, not something
        // raised around a paint (see ScrollTablePage::_claim_mode_row).
        _terminal.set_row_priority((uint16_t)(rows - 1), (uint16_t)(rows - 1),
                                   Terminal::PRIO_CHROME);
        _editor.redraw();                     // the page switch cleared the band
        _terminal.set_cursor_visible(true);
        _settle();
    }

    void NotesPage::deactivate() {
        // Leaving with unsaved edits would lose them at the next reset, and
        // nothing else is going to ask.
        if (_editor.dirty()) _save();
        _terminal.set_cursor_visible(false);
    }

    void NotesPage::_load() {
        size_t n = 0;
        if (!HAL::Storage::get(STORAGE_KEY, _editor.load_buffer(),
                               _editor.load_capacity(), &n))
            n = 0;                            // no note yet: start empty
        _truncated = !_editor.commit_loaded((uint16_t)n);
    }

    void NotesPage::_save() {
        bool ok = HAL::Storage::set(STORAGE_KEY,
                                    _editor.buffer(), _editor.buffer_len());
        _save_failed = !ok;
        if (ok) {
            _editor.clear_dirty();
        } else if (_logger != nullptr) {
            _logger->critical(Logs::STORAGE, "notes not saved\n"
                              "storage refused %u bytes",
                              (unsigned)_editor.buffer_len());
        }
    }

    void NotesPage::_draw_status() {
        uint16_t rows = _console.rows();
        if (rows < 3) return;
        uint16_t row  = (uint16_t)(rows - 1);

        char line[Config::TERMINAL_MAX_COLS];
        const char* state = _save_failed ? "NOT SAVED"
                          : _editor.dirty() ? "modified" : "saved";
        snprintf(line, sizeof(line), " %u/%u bytes  %s%s  ^O save",
                 (unsigned)_editor.buffer_len(), (unsigned)(Config::NOTES_LEN - 1),
                 state, _truncated ? "  (truncated on load)" : "");

        _terminal.move_to({row, 0});
        _terminal.set_inverted(true);
        _terminal.write_chars(line, (uint16_t)strlen(line));
        _terminal.fill_spaces(_terminal.cols());
        _terminal.set_inverted(false);
    }
}}

#endif // BOTANIK_FEATURE_NOTES_PAGE
