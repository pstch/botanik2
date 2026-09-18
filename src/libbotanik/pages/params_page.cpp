/**
 * @file params_page.cpp
 * @brief ParamsPage implementation - the table, the detail panel, the inline editor.
 */
#include "libbotanik/pages/params_page.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_PARAMS_PAGE

#include <cstdio>
#include <cstring>

#include "libbotanik/hal/platform.h"

namespace Botanik { namespace Pages {

    void ParamsPage::handle_command(Command cmd, uint16_t count) {
        if (_editing) { _handle_edit_command(cmd, count); return; }
        if (_filter_command(cmd)) { _dirty = true; return; }   // BACKSPACE pops, ESC clears
        if (_nav_command(cmd)) { _dirty = true; return; }
        if (cmd == Command::ENTER && _visible_count() > 0) { _start_edit(); _dirty = true; }
        // ^O writes, as on the NOTES page: the same key means the same thing on
        // both pages that hold unsaved work.
        if (cmd == Command::CTRL_O) _save_all();
    }

    // Every unsaved row at once, not the selected one: editing several settings
    // and committing them together is the normal shape of a tuning session, and
    // a per-row save would make "did I save that one?" a question.
    void ParamsPage::_save_all() {
        if (_persist == nullptr) return;
        uint16_t refused = 0;
        (void)_persist->save_all(&refused);
        _save_failed = (refused != 0);
        _dirty = true;              // the '*' markers clear with the same repaint
        _draw_prompt_row();
    }

    void ParamsPage::handle_text(char c) {
        if (!_editing) { if (_filter_text(c)) _dirty = true; return; }
        if (c < 0x20 || c == 0x7F) return;          // printable only
        if (_edit_error) _clear_error();
        _editor.insert_characters(&c, 1);
    }

    void ParamsPage::focus() {
        // The filter badge acknowledges a keypress, so its row outranks the
        // table (re-declared here: the Console resets the band on a switch).
        _claim_legend_row(_legend_row());
        _editing = false;
        _dirty = true;
        if (_selected >= _visible_count()) _selected = 0;
        _follow_selection();
    }

    void ParamsPage::tick(bool active) {
        if (!active) return;
        // While editing, the editor owns the bottom row (redrawn per
        // keystroke); leave the static table + panel as drawn.
        if (_editing) return;
        uint64_t now = HAL::Clock::uptime_ms();
        if (!_dirty && now - _last_refresh_ms < Config::PAGE_HOT_MS) return;
        _last_refresh_ms = now;
        _dirty = false;
        uint16_t n = _visible_count();
        _clamp_selection();
        _follow_selection();
        _draw_structure();
        uint16_t win = _window();
        for (uint16_t vi = 0; vi < win; vi++) {
            uint16_t row = (uint16_t)(DATA_TOP + vi), idx = (uint16_t)(_top + vi);
            if (idx < n) _draw_row(idx, row, idx == _selected);
            else { _terminal.move_to({row, 0}); _terminal.set_inverted(false); _terminal.clear_to_eol(); }
        }
        _draw_panel();
        _draw_prompt_row();
    }

    void ParamsPage::_on_filter_changed() {
        _editing = false;
        _dirty = true;
        _vis_ready = false;   // a narrower table holds a different count
    }

    uint16_t ParamsPage::_param_at(uint16_t row) const {
        // Unfiltered, the visible row IS the parameter index; the scan below
        // would walk the whole table per drawn row to rediscover that.
        if (_filter_n == 0) return (row < _param_count()) ? row : _param_count();
        uint16_t seen = 0;
        for (uint16_t k = 0; k < _param_count(); k++) {
            if (!_visible(k)) continue;
            if (seen == row) return k;
            seen++;
        }
        return _param_count();
    }

    uint16_t ParamsPage::_visible_count() const {
        if (_vis_ready) return _vis_count;
        uint16_t n = 0;
        for (uint16_t k = 0; k < _param_count(); k++) if (_visible(k)) n++;
        _vis_count = n;
        _vis_ready = true;
        return n;
    }

    uint8_t ParamsPage::_columns(Col* out) const {
        out[C_NAME]     = Col{ "PARAMETER", NAME_W     };
        out[C_TYPE]     = Col{ "TYPE",      TYPE_W     };
        out[C_VALUE]    = Col{ "VALUE",     0          };   // flex: the base fits it
        out[C_UNIT]     = Col{ "UNIT",      UNIT_W     };
        out[C_REGISTER] = Col{ "REGISTER",  REGISTER_W };
        return 5;
    }

    void ParamsPage::_draw_row(uint16_t idx, uint16_t row, bool selected) {
        const uint16_t k = _param_at(idx);
        Vars::Var p = _params->param_at(k);
        const Vars::Descriptor& d = p.meta();
        char val[VAL_CAP], reg[8], line[LINE_CAP];
        // Use what format() RETURNS, not the buffer: a T_STR's live text is in
        // the pool and a T_ENUM's in flash, so neither writes into val, and
        // reading val would show the previous row's text.
        const char* v = Vars::var_format(_params->param_at(k), val, sizeof(val));
        // A trailing '*' on an edited-but-unsaved value: one column, and it is
        // the only thing distinguishing a setting that survives a reboot from
        // one that does not. Copied into val first, since format() may have
        // returned flash or pool text that must not be written through.
        if (p.param_unsaved()) {
            if (v != val) { strncpy(val, v, sizeof(val) - 2); val[sizeof(val) - 2] = 0; }
            const size_t n = strlen(val);
            if (n < sizeof(val) - 1) { val[n] = '*'; val[n + 1] = 0; }
            v = val;
        }
        // 0 is "no register": a parameter the slave does not publish, which is
        // a blank cell rather than a register an external master could quote.
        if (d.bind.reg != 0) snprintf(reg, sizeof(reg), "%u", (unsigned)d.bind.reg);
        else                 reg[0] = '\0';
        const char* cells[5] = {
            d.name, Vars::type_name(d.type), v,
            d.unit ? d.unit : "", reg
        };
        uint16_t n = _build_row(line, sizeof(line), cells);
        uint16_t name_l = 0, name_r = 0;
        _cell_span(C_NAME, name_l, name_r);      // one fit, both ends
        _draw_selected_line(row, line, n, name_l, name_r,
                            selected);
    }

    void ParamsPage::_draw_structure() {
        _blank_line(TOP_ROW);
        _draw_header(HEADER_ROW);
        _draw_rule(RULE_ROW, '+');
    }

    const char* ParamsPage::_selected_cell() const {
        if (_params == nullptr || _selected >= _visible_count()) return nullptr;
        return _params->param_at(_param_at(_selected)).meta().name;
    }

    void ParamsPage::_draw_panel() {
        uint16_t r = _panel_top();
        _draw_separator(r);
        _blank_line((uint16_t)(r + 1));     // breathe under the table
        if (_visible_count() == 0) {
            for (uint16_t i = 2; i < PANEL_H; i++)
                if ((uint16_t)(r + i) != _legend_row()) _blank_line((uint16_t)(r + i));
            _draw_legend(_legend_row(), "no match");
            return;
        }
        Vars::Var p = _params->param_at(_param_at(_selected));
        const Vars::Descriptor& d = p.meta();
        char b[24];
        char b2[24];
        // Through Var::band(), not the declared one: an end may name another
        // row, and showing the fallback while the operator edits the real one
        // is the worst of both.
        const Vars::Band band = p.band();
        // Minimum / Maximum / Default share one row: three facts about the row
        // already selected, and every panel row costs a table row.
        //
        // Unless there is no band. A T_STR or unbounded row would read
        // "Minimum : -   Maximum : -" and then clip the one field with
        // something to say, in the narrowest slot on the row - and those are
        // exactly the rows with a long default (a hostname, a dotted quad). So
        // Default takes the whole row when the other two have nothing.
        char b3[VAL_CAP];
        if (band.set())
            _panel_field3((uint16_t)(r + 2),
                          "Minimum", _fmt_bound(b,  sizeof(b),  d, band.lo),
                          PANEL_COL2,
                          "Maximum", _fmt_bound(b2, sizeof(b2), d, band.hi),
                          PANEL_COL3,
                          "Default", _fmt_default(b3, sizeof(b3), d));
        else
            _panel_field((uint16_t)(r + 2), "Default",
                         _fmt_default(b3, sizeof(b3), d));
        _panel_field((uint16_t)(r + 3), "Description", d.desc);
        _panel_field((uint16_t)(r + 4), "Notes",       d.notes);
        _blank_line((uint16_t)(r + 5));                     // blank above the legend
        _draw_legend(_legend_row(), nullptr);
    }

    void ParamsPage::_draw_label() {
        char label[48];
        uint16_t lw = _edit_label(label, sizeof(label));
        _terminal.move_to({_prompt_row(), 0});
        _terminal.write_chars(label, lw);
    }

    void ParamsPage::_draw_prompt_row() {
        uint16_t row = _prompt_row();
        if (_editing) {
            _terminal.set_inverted(false);
            _terminal.move_to({row, 0});
            _terminal.clear_to_eol();
            _draw_label();
            _editor.redraw();
            if (_edit_error) _draw_error_marker();
            _terminal.set_cursor_visible(true);
            return;
        }
        _terminal.set_cursor_visible(false);
        // No key legend: UP/DOWN/ENTER/ESC are not worth a permanent row. Only
        // what an operator cannot infer stays - that edits are live but not yet
        // durable, and that a save was refused.
        char hint[64];
        // Every other _params use in this class is guarded; these two were not,
        // and this one is reached from tick() on EVERY refresh - so the page
        // documented a null backend and faulted on the first frame of it.
        const uint16_t pending = (_params != nullptr) ? _params->unsaved_count() : 0;
        if (_save_failed)
            snprintf(hint, sizeof(hint), " NOT SAVED - storage refused it");
        else if (pending != 0)
            snprintf(hint, sizeof(hint), " %u unsaved (*) - ^O to save", (unsigned)pending);
        else
            hint[0] = '\0';
        _draw_line(row, hint, (uint16_t)strlen(hint));
    }

    void ParamsPage::_draw_error_marker() {
        _terminal.move_to(_editor.end_pos());
        const char* e = "  [invalid]";
        _terminal.write_chars(e, (uint16_t)strlen(e));
        _editor.place_cursor();
    }

    void ParamsPage::_clear_error() {
        _terminal.move_to(_editor.end_pos());
        _terminal.clear_to_eol();
        _edit_error = false;
        _editor.place_cursor();
    }

    void ParamsPage::_start_edit() {
        if (_selected >= _visible_count()) return;
        _edit_index = _param_at(_selected);   // absolute, so a view change cannot move it
        _draw_panel();            // pin the panel to the parameter being edited
        uint16_t row = _prompt_row();
        char label[48];
        uint16_t lw = _edit_label(label, sizeof(label));

        _editor.as_field(row, lw);
        _editor.reset();          // clears the row past the label
        _draw_label();

        char val[80];
        const char* fv = Vars::var_format(_params->param_at(_edit_index), val, sizeof(val));
        const int n = (int)strlen(fv);
        if (fv != val) { strncpy(val, fv, sizeof(val) - 1); val[sizeof(val) - 1] = 0; }
        if (n > 0) _editor.insert_characters(val, (uint16_t)n);
        else       _editor.place_cursor();

        _edit_error = false;
        _editing = true;
        _terminal.set_cursor_visible(true);
    }

    void ParamsPage::_cancel_edit() {
        _editing = false;
        _dirty = true;
        _draw_prompt_row();       // restore the hint, hide the cursor
    }

    void ParamsPage::_commit_edit() {
        char line[Config::SHELL_INPUT_LEN];
        uint16_t n = _editor.buffer_len();
        if (n > (uint16_t)(sizeof(line) - 1)) n = (uint16_t)(sizeof(line) - 1);
        memcpy(line, _editor.buffer(), n);
        line[n] = 0;
        const Vars::SetResult r = _params->param_at(_edit_index).param_set(line);
        if (r != Vars::SET_OK) {
            _edit_error = true;   // stay in edit, flag it
            _draw_error_marker();
            return;
        }
        // Applied, and live - but not durable until ^O. The row's '*' and the
        // prompt row both say so, so leaving the editor loses nothing.
        _save_failed = false;
        _editing = false;
        _dirty = true;            // value diff repaints the table row next tick
        _draw_prompt_row();
    }

    void ParamsPage::_handle_edit_command(Command cmd, uint16_t count) {
        if (cmd == Command::ENTER) { _commit_edit(); return; }
        if (cmd == Command::ESC)   { _cancel_edit(); return; }
        const EditKey k = _edit_kind(_editor, cmd);
        if (k == EditKey::IGNORED) return;
        // Before the edit, not after: the marker is drawn from the field's END,
        // which the edit is about to move.
        if (k == EditKey::CHANGED && _edit_error) _clear_error();
        _edit_apply(_editor, cmd, count);
    }
}}

#endif // BOTANIK_FEATURE_PARAMS_PAGE
