/**
 * @file params_page.cpp
 * @brief ParamsPage implementation - the table, the detail panel, the inline editor.
 */
#include "libbotanik/pages/params_page.h"

#include <cstdio>                           // snprintf
#include <cstring>                          // strlen / strcmp / memcpy

#include "libbotanik/hal/platform.h"        // HAL::Clock::uptime_ms

namespace Botanik { namespace Pages {

    void ParamsPage::handle_command(Command cmd, uint16_t count) {
        if (_editing) { _handle_edit_command(cmd, count); return; }
        if (_mode_command(cmd)) return;             // LEFT/RIGHT pick the group
        if (_nav_command(cmd)) { _dirty = true; return; }
        if (cmd == Command::ENTER && _visible_count() > 0) { _start_edit(); _dirty = true; }
    }

    void ParamsPage::handle_text(char c) {
        if (!_editing && _mode_text(c)) return;     // SPACE: next group
        if (!_editing) return;
        if (c < 0x20 || c == 0x7F) return;          // printable only
        if (_edit_error) _clear_error();
        _editor.insert_characters(&c, 1);
    }

    void ParamsPage::focus() {
        // The group badge acknowledges a keypress, so its row outranks the
        // table (re-declared here: the Console resets the band on a switch).
        _claim_mode_row(_mode_row());
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

    void ParamsPage::_ensure_groups() const {
        if (_groups_ready) return;
        _groups_ready = true;
        _group_n = 0;
        for (uint16_t i = 0; i < _param_count() && _group_n < GROUPS_MAX; i++) {
            // Interned by _group_of(), so the pointer outlives the call.
            const char* g = _group_of(*_params->param(i).meta());
            bool seen = false;
            for (uint8_t k = 0; k < _group_n; k++)
                if (strcmp(_group_name[k], g) == 0) { seen = true; break; }
            if (!seen) _group_name[_group_n++] = g;
        }
    }

    const char* ParamsPage::_group_intern(const char* g) {
        static char pool[GROUPS_MAX][Config::VAR_GROUP_NAME_LEN];
        static uint8_t used = 0;
        for (uint8_t i = 0; i < used; i++)
            if (strcmp(pool[i], g) == 0) return pool[i];
        if (used >= GROUPS_MAX) return "?";
        char* dst = pool[used++];
        size_t n = strlen(g);
        if (n >= sizeof(pool[0])) n = sizeof(pool[0]) - 1;
        memcpy(dst, g, n); dst[n] = 0;
        return dst;
    }

    const char* ParamsPage::_mode_label(uint8_t i) const {
        if (i == 0) return "all";
        _ensure_groups();
        return (i <= _group_n) ? _group_name[i - 1] : "all";
    }

    void ParamsPage::_on_mode_changed() {
        _editing = false;
        _dirty = true;
        _vis_ready = false;   // a different view shows a different count
    }

    uint16_t ParamsPage::_param_at(uint16_t row) const {
        // In "all" the visible row IS the parameter index; the scan below
        // would walk the whole table per drawn row to rediscover that.
        if (_mode == 0) return (row < _param_count()) ? row : _param_count();
        uint16_t seen = 0;
        for (uint16_t k = 0; k < _param_count(); k++) {
            if (!_in_mode(k)) continue;
            if (seen == row) return k;
            seen++;
        }
        return _param_count();
    }

    uint16_t ParamsPage::_visible_count() const {
        if (_vis_ready) return _vis_count;
        uint16_t n = 0;
        for (uint16_t k = 0; k < _param_count(); k++) if (_in_mode(k)) n++;
        _vis_count = n;
        _vis_ready = true;
        return n;
    }

    uint8_t ParamsPage::_columns(Col* out) const {
        out[C_NAME]    = Col{ "PARAMETER", NAME_W     };
        out[C_TYPE]    = Col{ "TYPE",      TYPE_W     };
        out[C_VALUE]   = Col{ "VALUE",     _value_w() };
        out[C_DEFAULT] = Col{ "DEFAULT",   DEFAULT_W  };
        out[C_UNIT]    = Col{ "UNIT",      UNIT_W     };
        return 5;
    }

    void ParamsPage::_draw_row(uint16_t idx, uint16_t row, bool selected) {
        const uint16_t k = _param_at(idx);
        Vars::Var p = _params->param(k);
        const Vars::Variable& d = *p.meta();
        char val[VAL_CAP], dflt[VAL_CAP], line[LINE_CAP];
        // Use what format() RETURNS, not the buffer: a T_STR's live text
        // lives in the pool and a T_ENUM's in flash, so neither writes into
        // val - and reading val anyway showed the previous row's text, which
        // is only visible once a string parameter sits under a numeric one.
        const char* v = _params->format(k, val, sizeof(val));
        const char* dv = _params->format_scalar_of(d, d.def, dflt, sizeof(dflt));
        const char* cells[5] = {
            d.name, Vars::type_name(d.type), v,
            (d.type == Vars::T_STR && d.def.s) ? d.def.s : dv,
            d.unit ? d.unit : ""
        };
        uint16_t n = _build_row(line, sizeof(line), cells);
        _draw_selected_line(row, line, n, _cell_col(C_NAME), _cell_end(C_NAME),
                            selected);
    }

    void ParamsPage::_draw_structure() {
        _blank_line(TOP_ROW);
        _draw_header(HEADER_ROW);
        _draw_rule(RULE_ROW, '+');
    }

    const char* ParamsPage::_group_of(const Vars::Variable& d) {
        const char* u = strchr(d.name, '_');
        if (u == nullptr) return d.name;          // the serial itself
        char g[Config::VAR_GROUP_NAME_LEN];
        size_t n = (size_t)(u - d.name);
        if (n >= sizeof(g)) n = sizeof(g) - 1;
        memcpy(g, d.name, n); g[n] = 0;
        // Interned, NOT a pointer into a shared static: the legend passes
        // the selected row's group to _draw_mode_line, which then asks for
        // the neighbouring view names - and that walks every parameter
        // through here. A shared buffer was overwritten mid-call, so the
        // legend always showed the LAST group in the table whatever was
        // selected.
        return _group_intern(g);
    }

    uint16_t ParamsPage::_selected_cell(char* out, uint16_t cap) const {
        if (_params == nullptr || _selected >= _visible_count()) return 0;
        return _copy_cell(out, cap,
                          _params->param(_param_at(_selected)).meta()->name);
    }

    void ParamsPage::_draw_panel() {
        uint16_t r = _panel_top();
        _draw_separator(r);
        _blank_line((uint16_t)(r + 1));     // breathe under the table
        if (_visible_count() == 0) {
            for (uint16_t i = 2; i < PANEL_H; i++)
                if ((uint16_t)(r + i) != _mode_row()) _blank_line((uint16_t)(r + i));
            _draw_mode_line(_mode_row(), nullptr, nullptr);
            return;
        }
        Vars::Var p = _params->param(_param_at(_selected));
        const Vars::Variable& d = *p.meta();
        char b[24];
        // The band is one fact, so it costs one row - and the row that buys
        // back is a row of the table.
        char b2[24];
        // The blank that used to sit under the separator paid for the Notes
        // row: the separator already separates, so the table keeps its row.
        // Through var_band(), not num_band(): an analog row's band can live
        // in parameters, and showing the firmware defaults while the
        // operator edits the real ones is the worst of both.
        const Vars::NumBand band =
            Vars::var_band(_params != nullptr ? _params->bank() : nullptr, d);
        // ...and the register rides along on the same row. It is the one
        // number about a parameter that is an EXTERNAL contract - a Modbus
        // master's map is written against it - and it was reachable only by
        // reading the site's table. Hand-assigned per row, so it is also the
        // one an operator may need to read back and quote.
        char b3[8];
        snprintf(b3, sizeof(b3), "%u", (unsigned)d.bind.reg);
        _panel_field3((uint16_t)(r + 2),
                      "Minimum", _fmt_bound(b,  sizeof(b),  d, band.min),
                      PANEL_COL2,
                      "Maximum", _fmt_bound(b2, sizeof(b2), d, band.max),
                      PANEL_COL3,
                      "Register", d.bind.reg != 0 ? b3 : nullptr);
        _panel_field((uint16_t)(r + 3), "Description", d.desc);
        _panel_field((uint16_t)(r + 4), "Notes",       d.notes);
        _blank_line((uint16_t)(r + 5));                     // blank above the legend
        // The group is on the legend line instead of costing a panel row -
        // and only in the "all" view, where it is the one thing the badge
        // does not already say. The prompt row below is the legend's blank
        // frame while nothing is being edited.
        _draw_mode_line(_mode_row(), _mode == 0 ? _group_of(d) : nullptr, nullptr);
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
        // No key legend: UP/DOWN/ENTER/ESC on a table are not worth a
        // permanent row. Only the one thing an operator cannot infer stays -
        // that a value they just set did not reach storage.
        const char* hint = _not_persisted
            ? " NOT SAVED - value is live but storage refused it" : "";
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

        const char* empty = "";
        _editor.set_ps1(empty, 0);
        _editor.set_origin(row);
        _editor.set_indent(lw);
        _editor.set_single_line(true);
        _editor.reset();          // clears the row (label included)
        _draw_label();

        char val[80];
        const char* fv = _params->format(_edit_index, val, sizeof(val));
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
        const Vars::SetResult r = _params->set(_params->param(_edit_index), line);
        if (r == Vars::SET_INVALID) {
            _edit_error = true;   // stay in edit, flag it
            _draw_error_marker();
            return;
        }
        // Applied. SET_NOT_PERSISTED means it is live but will not survive a
        // reboot - leaving the operator stuck in the editor would suggest the
        // input was wrong, which it was not; the critical log carries it.
        _not_persisted = (r == Vars::SET_NOT_PERSISTED);
        _editing = false;
        _dirty = true;            // value diff repaints the table row next tick
        _draw_prompt_row();
    }

    void ParamsPage::_handle_edit_command(Command cmd, uint16_t count) {
        switch (cmd) {
            case Command::ENTER:      _commit_edit();               break;
            case Command::ESC:        _cancel_edit();               break;
            case Command::BACKSPACE:
                if (_edit_error) _clear_error();
                _editor.backspace(count);                           break;
            case Command::DELETE:
            case Command::CTRL_D:
                if (_edit_error) _clear_error();
                _editor.delete_forward(count);                      break;
            case Command::LEFT:  case Command::CTRL_B:
                _editor.move_left();                                break;
            case Command::RIGHT: case Command::CTRL_F:
                _editor.move_right();                               break;
            case Command::HOME:  case Command::CTRL_A:
                _editor.move_to_line_start();                       break;
            case Command::END:   case Command::CTRL_E:
                _editor.move_to_line_end();                         break;
            case Command::CTRL_U:
                if (_edit_error) _clear_error();
                _editor.move_to_line_start();
                _editor.delete_forward(_editor.buffer_len());       break;
            // Same cut/paste as the shell prompt, same ring: Alt+W on the table
            // puts a name in it, and this is the field it gets pasted into.
            case Command::CTRL_K:
                if (_edit_error) _clear_error();
                _editor.kill_to_line_end();                         break;
            case Command::CTRL_W:
                if (_edit_error) _clear_error();
                _editor.kill_word_back();                           break;
            case Command::CTRL_Y:
                if (_edit_error) _clear_error();
                _editor.yank();                                     break;
            default: break;
        }
    }
}}
