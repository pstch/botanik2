// REVIEW_35: TODO
#ifndef PAGES_PARAMS_PAGE_H
#define PAGES_PARAMS_PAGE_H

/**
 * @file params_page.h
 * @brief ParamsPage - interactive table of configuration parameters.
 *
 * Layout (row 0 is the terminal status bar, the bottom row the alarm bar); the
 * same bordered shape as AlarmsPage - a blank top margin and blank columns each
 * side, widened to the display:
 *   row 1            blank top margin
 *   row 2            header  (| PARAMETER | TYPE | VALUE | DEFAULT | UNIT |)
 *   row 3            top rule
 *   rows 4..K        one row per Param (windowed / scrollable)
 *   rows K..         detail panel for the selected parameter: a separator then
 *                    Minimum / Maximum / Description / Notes
 *   rows()-1         edit prompt / hint line (just above the alarm bar)
 *
 * UP/DOWN move a selection cursor (the selected row is drawn inverted); the
 * detail panel tracks it. ENTER opens an inline edit on the bottom prompt line:
 * a static " name = " label and a single-line TextEditor (anchored to the
 * bottom row) render the value, seeded with the row's current text. ENTER
 * commits (parse + validate through Params::set, which persists it; applying a
 * config group still means bumping that group's commit serial by hand);
 * ESC/Ctrl+C cancels. An invalid value is rejected and
 * flagged, leaving edit mode open. Rows repaint only on a change, so a steady
 * screen spends no TX budget.
 */

#include <cstdint>                    // uint8_t/uint16_t/uint64_t - cursor, edit state, commit stamps
#include <cstdio>                     // snprintf - the row cells and the detail panel
#include <cstring>                    // memcpy / strchr / strcmp / strlen - the inline value editor

#include "libbotanik_config.h"        // BOTANIK_FEATURE_PARAMS_PAGE - the gate this header sits behind

#include "libbotanik/console/console.h" // Console::rows / terminal - the content area to paint
#include "libbotanik/console/editor.h" // Terminal::TextEditor - the bottom-row value editor
#include "libbotanik/console/terminal.h" // AnsiTerminal - the drawing operations
#include "libbotanik/pages/page.h"    // Pages::Page - the interface this page implements
#include "libbotanik/pages/scroll_table.h" // Pages::ScrollTablePage - the scrolling table base
#include "libbotanik/vars/params.h"   // Vars::Params - the dense parameter view this page edits

namespace Botanik { namespace Pages {

    /** @brief Full-screen editable configuration table (see file header). */
    class ParamsPage : public ScrollTablePage {
    private:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Console      = Terminal::Console;
        using InputEditor  = Terminal::TextEditor;
        using Command      = Terminal::InputEvent::Command;

        /** @name Layout
         *  @{ Bordered table with the same shape as AlarmsPage: a blank top
         *  margin row, MARGIN blank columns each side, widened to the display. */
        static constexpr uint16_t TOP_ROW    = 1;    // blank top margin
        static constexpr uint16_t HEADER_ROW = 2;
        static constexpr uint16_t RULE_ROW   = 3;
        static constexpr uint16_t DATA_TOP   = 4;    // first parameter row
        // Wide enough for the longest name today plus headroom (see monitor.h).
        static constexpr uint16_t NAME_W     = 24;   // parameter-name column
        static constexpr uint16_t TYPE_W     = 4;    // data-type column (u16/enum/f32/b)
        static constexpr uint16_t DEFAULT_W  = 12;   // default-value column
        static constexpr uint16_t UNIT_W     = 5;    // physical-unit column
        static constexpr uint16_t VAL_CAP    = 40;   // formatted value cap
        static constexpr uint16_t LINE_CAP   = Config::TABLE_LINE_LEN;
        // Detail panel below the table: a separator rule, a blank spacer, then
        // Minimum / Maximum / Description / Notes for the selected parameter, and
        // a trailing blank spacer (default + unit are already table columns).
        // ... then a blank, the [group] legend, and a blank - the same framing
        // the ALARMS table gives its mode badge.
        static constexpr uint16_t PANEL_H    = 7;
        /** @brief Column order; the selection cursor inverts C_NAME's cell. */
        enum { C_NAME = 0, C_TYPE, C_VALUE, C_DEFAULT, C_UNIT };
        /** @} */

        /** @name State
         *  @{ */
        Console&            _console;
        Vars::Params* _params;
        bool                _dirty    = true;    ///< table needs a redraw
        uint64_t            _last_refresh_ms = 0; ///< last cadence rebuild

        /** @name Edit sub-state (bottom-row inline editor) @{ */
        /** buffers the editor borrows; declared before it */
        Terminal::EditorBuffers<Config::SHELL_INPUT_LEN,
                                Config::EDITOR_MAX_ROWS> _editor_bufs;
        InputEditor         _editor;                 ///< single-line value editor
        bool                _editing    = false;
        uint16_t _edit_index = 0;   ///< the parameter being edited
        bool                _edit_error = false;      ///< last commit rejected
        /** last commit applied but did not reach storage (shown on the prompt
         *  row, since the value itself is live and the row will show it) */
        bool                _not_persisted = false;
        /** @} */
        /** @} */

    public:
        /** @brief @p console owns the content geometry; @p config is the backend
         *  the table reads/writes (may be null -> empty table). */
        ParamsPage(Console& console, Vars::Params* config)
            : ScrollTablePage(console.terminal()), _console(console), _params(config),
              _editor(&console, _editor_bufs.storage()) {}

        const char* name() const override { return "PARAMS"; }

        /** @name Input
         *  @{ */

        void handle_command(Command cmd, uint16_t count = 1) override {
            if (_editing) { _handle_edit_command(cmd, count); return; }
            if (_mode_command(cmd)) return;             // LEFT/RIGHT pick the group
            if (_nav_command(cmd)) { _dirty = true; return; }
            if (cmd == Command::ENTER && _visible_count() > 0) { _start_edit(); _dirty = true; }
        }

        void handle_text(char c) override {
            if (!_editing && _mode_text(c)) return;     // SPACE: next group
            if (!_editing) return;
            if (c < 0x20 || c == 0x7F) return;          // printable only
            if (_edit_error) _clear_error();
            _editor.insert_characters(&c, 1);
        }

        bool handle_cancel() override {
            if (_editing) { _cancel_edit(); return true; }   // Ctrl+C leaves edit
            return false;
        }

        /** @} */

        /** @name Focus & update
         *  @{ */

        void deactivate() override {}

        void focus() override {
            // The group badge acknowledges a keypress, so its row outranks the
            // table (re-declared here: the Console resets the band on a switch).
            _claim_mode_row(_mode_row());
            _editing = false;
            _dirty = true;
            if (_selected >= _visible_count()) _selected = 0;
            _follow_selection();
        }

        void tick(bool active) override {
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

        /** @} */

    private:
        uint16_t _param_count() const { return _params != nullptr ? _params->count() : 0; }

        /** @name Group modes
         *  A parameter's group IS its name prefix (`net_`, `ntp_`, `mqtt_`) -
         *  the same prefix that names its commit serial - so the groups are
         *  discovered from the table rather than declared twice. Mode 0 shows
         *  every parameter; 1..N show one group each. SPACE / RIGHT step
         *  forward, LEFT back (scroll_table.h).
         *  @{ */
        static constexpr uint8_t GROUPS_MAX = Vars::VAR_GROUPS_MAX;

        /** @brief Distinct group prefixes, in table order, discovered ONCE.
         *
         *  The parameter table is fixed at build time, so the group set cannot
         *  move - but this used to be rebuilt inside _mode_label(), which
         *  _in_mode() calls per parameter, which _visible_count() calls per
         *  parameter. That is a walk of the whole table squared, on every
         *  repaint, and the table is now 148 rows. Mutable because the query
         *  side is const and this is a cache, not state. */
        mutable const char* _group_name[GROUPS_MAX];
        mutable uint8_t     _group_n      = 0;
        mutable bool        _groups_ready = false;

        void _ensure_groups() const {
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

        /** @brief _group_of() returns a pointer to its own static scratch, so a
         *  second call clobbers the first. Copy each distinct group once into a
         *  table-lifetime store the mode labels can keep pointing at. */
        static const char* _group_intern(const char* g) {
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

        uint8_t _mode_count() const override {
            _ensure_groups();
            return (uint8_t)(_group_n + 1);
        }
        const char* _mode_label(uint8_t i) const override {
            if (i == 0) return "all";
            _ensure_groups();
            return (i <= _group_n) ? _group_name[i - 1] : "all";
        }
        void _on_mode_changed() override {
            _editing = false;
            _dirty = true;
            _vis_ready = false;   // a different view shows a different count
        }

        /** @brief True when parameter @p k belongs to the active view. */
        bool _in_mode(uint16_t k) const {
            if (_mode == 0) return true;
            return strcmp(_group_of(*_params->param(k).meta()), _mode_label(_mode)) == 0;
        }

        /** @brief Map a visible row to its parameter index (VAR_NONE-ish: the
         *  count when out of range). */
        uint16_t _param_at(uint16_t row) const {
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

        /** @brief Parameters the active view shows. Cached: a parameter's group
         *  is its name prefix, so membership is fixed for the life of the build
         *  and only a view change can move this. */
        mutable uint16_t _vis_count = 0;
        mutable bool     _vis_ready = false;
        uint16_t _visible_count() const {
            if (_vis_ready) return _vis_count;
            uint16_t n = 0;
            for (uint16_t k = 0; k < _param_count(); k++) if (_in_mode(k)) n++;
            _vis_count = n;
            _vis_ready = true;
            return n;
        }
        /** @} */

        /** @brief Row the bottom edit/hint prompt sits on (above the alarm bar). */
        uint16_t _prompt_row() const { return (uint16_t)(_console.rows() - 1); }

        /** @brief The group badge sits on the panel's trailing spacer, one row
         *  above the prompt - so an open editor, which owns the prompt row,
         *  never covers it. */
        uint16_t _mode_row() const { return (uint16_t)(_panel_top() + PANEL_H - 1); }
        /** @brief Columns the panel's second and third fields start at
         *  (Minimum | Maximum | Register). COL3 is where the Maximum field's
         *  widest value ends, so the three fit an 80-column row. */
        static constexpr uint16_t PANEL_COL2 = 38;
        static constexpr uint16_t PANEL_COL3 = 60;



        /** @brief First row of the detail panel (separator), reserving PANEL_H
         *  rows above the prompt. */
        uint16_t _panel_top() const {
            uint16_t pr = _prompt_row();
            return (pr > (uint16_t)(DATA_TOP + PANEL_H)) ? (uint16_t)(pr - PANEL_H) : DATA_TOP;
        }

        /** @brief Rows available for the parameter window (table shrinks to leave
         *  room for the detail panel). */
        uint16_t _visible_rows() const {
            uint16_t pt = _panel_top();
            return (pt > DATA_TOP) ? (uint16_t)(pt - DATA_TOP) : 0;
        }

        /** @name ScrollTablePage geometry @{ */
        uint16_t _data_top()  const override { return DATA_TOP; }
        uint16_t _window()    const override { return _visible_rows(); }
        uint16_t _row_count() const override { return _visible_count(); }
        /** @} */

        // Table rendering (5 columns: name | type | value | default | unit)
        // --------------------------------------------------------------------

        /** @brief Flexible VALUE width: the display minus the four fixed columns
         *  and every border, floored at 8 and capped at VAL_CAP-1. */
        uint16_t _value_w() const {
            return _flex_width((uint16_t)(MARGIN + 2 + NAME_W + 3 + TYPE_W + 3 + 3
                                          + DEFAULT_W + 3 + UNIT_W + 2), (uint16_t)(VAL_CAP - 1));
        }

        uint8_t _columns(Col* out) const override {
            out[C_NAME]    = Col{ "PARAMETER", NAME_W     };
            out[C_TYPE]    = Col{ "TYPE",      TYPE_W     };
            out[C_VALUE]   = Col{ "VALUE",     _value_w() };
            out[C_DEFAULT] = Col{ "DEFAULT",   DEFAULT_W  };
            out[C_UNIT]    = Col{ "UNIT",      UNIT_W     };
            return 5;
        }

        void _draw_row(uint16_t idx, uint16_t row, bool selected) {
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

        void _draw_structure() {
            _blank_line(TOP_ROW);
            _draw_header(HEADER_ROW);
            _draw_rule(RULE_ROW, '+');
        }

        // Detail panel (selected parameter's min/max/description/notes)
        // --------------------------------------------------------------------

        /** @brief The config group a parameter belongs to: the prefix of its
         *  name, which is also the name of the group's commit serial. */
        static const char* _group_of(const Vars::Variable& d) {
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

        /** @brief Format a bound, in the parameter's OWN type - so a real
         *  parameter's band shows its decimals instead of being truncated to
         *  whole numbers. "-" when there is no declared band. */
        const char* _fmt_bound(char* buf, size_t cap, const Vars::Variable& d,
                               Vars::Scalar v) const {
            if (d.type == Vars::T_STR || (d.flags & Vars::F_BOUNDED) == 0) {
                snprintf(buf, cap, "-"); return buf;
            }
            const char* t = _params->format_scalar_of(d, v, buf, cap);
            if (t != buf) { strncpy(buf, t, cap - 1); buf[cap - 1] = 0; }
            return buf;
        }


        /** @brief Draw the detail panel for the selected parameter: a separator,
         *  a blank spacer, the four detail rows, and a trailing blank spacer. */
        void _draw_panel() {
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

        // Bottom prompt row: inline edit field or hint
        // --------------------------------------------------------------------

        /** @brief " name = " static label for the parameter being edited. */
        uint16_t _edit_label(char* buf, size_t cap) const {
            int n = snprintf(buf, cap, " %s = ", _params->param(_edit_index).meta()->name);
            return (n < 0) ? 0 : (uint16_t)n;
        }

        void _draw_label() {
            char label[48];
            uint16_t lw = _edit_label(label, sizeof(label));
            _terminal.move_to({_prompt_row(), 0});
            _terminal.write_chars(label, lw);
        }

        void _draw_prompt_row() {
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

        void _draw_error_marker() {
            _terminal.move_to(_editor.end_pos());
            const char* e = "  [invalid]";
            _terminal.write_chars(e, (uint16_t)strlen(e));
            _editor.place_cursor();
        }

        void _clear_error() {
            _terminal.move_to(_editor.end_pos());
            _terminal.clear_to_eol();
            _edit_error = false;
            _editor.place_cursor();
        }

        // Edit sub-state
        // --------------------------------------------------------------------

        void _start_edit() {
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

        void _cancel_edit() {
            _editing = false;
            _dirty = true;
            _draw_prompt_row();       // restore the hint, hide the cursor
        }

        void _commit_edit() {
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

        void _handle_edit_command(Command cmd, uint16_t count) {
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
                default: break;
            }
        }
    };

}}

#endif // PAGES_PARAMS_PAGE_H
