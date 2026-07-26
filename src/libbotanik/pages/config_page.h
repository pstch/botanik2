// REVIEW_35: TODO
#ifndef PAGES_CONFIG_PAGE_H
#define PAGES_CONFIG_PAGE_H

/**
 * @file config_page.h
 * @brief ConfigPage - interactive table of configuration parameters.
 *
 * Layout (row 0 is the terminal status bar, the bottom row the alarm bar); the
 * same bordered shape as AlarmsPage - a blank top margin and blank columns each
 * side, widened to the display:
 *   row 1            blank top margin
 *   row 2            header  (| PARAMETER | TYPE | VALUE | DEFAULT | UNIT |)
 *   row 3            top rule
 *   rows 4..K        one row per ConfigParam (windowed / scrollable)
 *   rows K..         detail panel for the selected parameter: a separator then
 *                    Minimum / Maximum / Description / Notes
 *   rows()-1         edit prompt / hint line (just above the alarm bar)
 *
 * UP/DOWN move a selection cursor (the selected row is drawn inverted); the
 * detail panel tracks it. ENTER opens an inline edit on the bottom prompt line:
 * a static " name = " label and a single-line TextEditor (anchored to the
 * bottom row) render the value, seeded with the row's current text. ENTER
 * commits (parse + validate through ConfigHandler::set, which persists + bumps
 * the group serial); ESC/Ctrl+C cancels. An invalid value is rejected and
 * flagged, leaving edit mode open. Rows repaint only on a change, so a steady
 * screen spends no TX budget.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "libbotanik_config.h"
#include "libbotanik/core/config.h"
#include "libbotanik/pages/page.h"
#include "libbotanik/pages/scroll_table.h"
#include "libbotanik/console/console.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/console/editor.h"

namespace Botanik { namespace Pages {

    /** @brief Full-screen editable configuration table (see file header). */
    class ConfigPage : public ScrollTablePage {
    private:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Console      = Terminal::Console;
        using InputEditor  = Terminal::TextEditor;
        using Command      = Terminal::InputEvent::Command;

        /** @name Layout
         *  @{ Bordered table with the same shape as AlarmsPage: a blank top
         *  margin row, MARGIN blank columns each side, widened to the display. */
        static constexpr uint16_t MARGIN     = 2;    // blank columns each side
        static constexpr uint16_t TOP_ROW    = 1;    // blank top margin
        static constexpr uint16_t HEADER_ROW = 2;
        static constexpr uint16_t RULE_ROW   = 3;
        static constexpr uint16_t DATA_TOP   = 4;    // first parameter row
        static constexpr uint16_t NAME_W     = 18;   // parameter-name column
        static constexpr uint16_t TYPE_W     = 4;    // data-type column (u16/enum/f32/b)
        static constexpr uint16_t DEFAULT_W  = 12;   // default-value column
        static constexpr uint16_t UNIT_W     = 5;    // physical-unit column
        static constexpr uint16_t VAL_CAP    = 40;   // formatted value cap
        static constexpr uint16_t LINE_CAP   = 200;  // row string buffer
        // Detail panel below the table: a separator rule, a blank spacer, then
        // Minimum / Maximum / Description / Notes for the selected parameter, and
        // a trailing blank spacer (default + unit are already table columns).
        static constexpr uint16_t PANEL_H    = 7;
        // Selected-row highlight covers only the name field's contents.
        static constexpr uint16_t NAME_CELL_L = MARGIN + 2;            // "| " skipped
        static constexpr uint16_t NAME_CELL_R = MARGIN + 2 + NAME_W;   // name field end
        /** @} */

        /** @name State
         *  @{ */
        Console&            _console;
        Cfg::ConfigHandler* _config;
        bool                _dirty    = true;    ///< table needs a redraw
        uint64_t            _last_refresh_ms = 0; ///< last cadence rebuild

        /** @name Edit sub-state (bottom-row inline editor) @{ */
        InputEditor         _editor;                 ///< single-line value editor
        bool                _editing    = false;
        const Cfg::ConfigParam* _edit_param = nullptr;
        bool                _edit_error = false;      ///< last commit rejected
        /** @} */
        /** @} */

    public:
        /** @brief @p console owns the content geometry; @p config is the backend
         *  the table reads/writes (may be null -> empty table). */
        ConfigPage(Console& console, Cfg::ConfigHandler* config)
            : ScrollTablePage(console.terminal()), _console(console), _config(config),
              _editor(&console) {}

        const char* name() const override { return "CONFIG"; }

        /** @name Input
         *  @{ */

        void handle_text(char c) override {
            if (!_editing) return;
            if (c < 0x20 || c == 0x7F) return;           // printable only
            if (_edit_error) _clear_error();
            _editor.insert_characters(&c, 1);
        }

        void handle_command(Command cmd, uint16_t count = 1) override {
            if (_editing) { _handle_edit_command(cmd, count); return; }
            if (_nav_command(cmd)) { _dirty = true; return; }
            if (cmd == Command::ENTER && _param_count() > 0) { _start_edit(); _dirty = true; }
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
            _editing = false;
            _dirty = true;
            if (_selected >= _param_count()) _selected = 0;
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
            uint16_t n = _param_count();
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
        uint16_t _param_count() const { return _config != nullptr ? _config->count() : 0; }

        /** @brief Row the bottom edit/hint prompt sits on (above the alarm bar). */
        uint16_t _prompt_row() const { return (uint16_t)(_console.rows() - 1); }

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
        uint16_t _row_count() const override { return _param_count(); }
        /** @} */

        // Table rendering (5 columns: name | type | value | default | unit)
        // --------------------------------------------------------------------

        /** @brief Flexible VALUE width: the display minus the four fixed columns
         *  and every border, floored at 8 and capped at VAL_CAP-1. */
        uint16_t _value_w() const {
            uint16_t used = (uint16_t)(MARGIN + 2 + NAME_W + 3 + TYPE_W + 3 + 3
                                       + DEFAULT_W + 3 + UNIT_W + 2);
            uint16_t cols = _terminal.cols();
            uint16_t w = (cols > used) ? (uint16_t)(cols - used) : 8;
            if (w < 8) w = 8;
            if (w > (uint16_t)(VAL_CAP - 1)) w = (uint16_t)(VAL_CAP - 1);
            return w;
        }

        /** @brief Build the "  | name | type | value | default | unit |" row. */
        uint16_t _row5(char* out, size_t cap, const char* name_, const char* type_,
                       const char* val, const char* deflt, const char* unit) const {
            int n = snprintf(out, cap, "%*s| %-*.*s | %-*.*s | %-*.*s | %-*.*s | %-*.*s |",
                             (int)MARGIN, "",
                             NAME_W, NAME_W, name_, TYPE_W, TYPE_W, type_,
                             _value_w(), _value_w(), val,
                             DEFAULT_W, DEFAULT_W, deflt, UNIT_W, UNIT_W, unit);
            return (n < 0) ? 0 : (uint16_t)n;
        }

        void _draw_row(uint16_t idx, uint16_t row, bool selected) {
            const Cfg::ConfigParam& p = _config->param((uint8_t)idx);
            char val[VAL_CAP];
            _config->format((uint8_t)idx, val, sizeof(val));
            char line[LINE_CAP];
            uint16_t n = _row5(line, sizeof(line), p.name, Cfg::type_name(p), val,
                               p.deflt ? p.deflt : "", p.unit ? p.unit : "");
            _draw_selected_line(row, line, n, NAME_CELL_L, NAME_CELL_R, selected);
        }

        void _draw_header() {
            char line[LINE_CAP];
            uint16_t n = _row5(line, sizeof(line), "PARAMETER", "TYPE", "VALUE",
                               "DEFAULT", "UNIT");
            _draw_line(HEADER_ROW, line, n);
        }

        void _draw_rule() {
            uint16_t spans[5] = { (uint16_t)(NAME_W + 2), (uint16_t)(TYPE_W + 2),
                                  (uint16_t)(_value_w() + 2), (uint16_t)(DEFAULT_W + 2),
                                  (uint16_t)(UNIT_W + 2) };
            ScrollTablePage::_draw_rule(RULE_ROW, MARGIN, spans, 5, '+', '+');
        }

        void _draw_structure() {
            _blank_line(TOP_ROW);
            _draw_header();
            _draw_rule();
        }

        // Detail panel (selected parameter's min/max/description/notes)
        // --------------------------------------------------------------------

        /** @brief Format a numeric bound in display units ("-" for strings). */
        const char* _fmt_bound(char* buf, size_t cap, const Cfg::ConfigParam& p,
                               int32_t v) const {
            if (Cfg::is_string(p)) { snprintf(buf, cap, "-"); return buf; }
            if (p.divisor > 1)
                snprintf(buf, cap, "%.*f", (int)p.decimals, (double)v / (double)p.divisor);
            else
                snprintf(buf, cap, "%ld", (long)v);
            return buf;
        }

        /** @brief A full-width dashed separator between the table and the panel. */
        void _draw_separator(uint16_t row) {
            char buf[LINE_CAP];
            uint16_t n = 0;
            for (uint16_t i = 0; i < MARGIN && n < LINE_CAP - 1; i++) buf[n++] = ' ';
            uint16_t cols = _terminal.cols();
            uint16_t w = (cols > (uint16_t)(2 * MARGIN)) ? (uint16_t)(cols - 2 * MARGIN) : 0;
            for (uint16_t i = 0; i < w && n < LINE_CAP - 1; i++) buf[n++] = '-';
            _draw_line(row, buf, n);
        }

        /** @brief " Label       : value" detail line (clear_to_eol wipes the old
         *  value's tail when the selection moves to a shorter one). */
        void _panel_field(uint16_t row, const char* label, const char* value) {
            char buf[128];
            int n = snprintf(buf, sizeof(buf), " %-11s : %s", label,
                             (value && value[0]) ? value : "-");
            _draw_line(row, buf, (n < 0) ? 0 : (uint16_t)n);
        }

        /** @brief Draw the detail panel for the selected parameter: a separator,
         *  a blank spacer, the four detail rows, and a trailing blank spacer. */
        void _draw_panel() {
            uint16_t r = _panel_top();
            _draw_separator(r);
            _blank_line((uint16_t)(r + 1));
            if (_param_count() == 0) {
                for (uint16_t i = 2; i < PANEL_H; i++) _blank_line((uint16_t)(r + i));
                return;
            }
            const Cfg::ConfigParam& p = _config->param((uint8_t)_selected);
            char b[24];
            _panel_field((uint16_t)(r + 2), "Minimum",     _fmt_bound(b, sizeof(b), p, p.min));
            _panel_field((uint16_t)(r + 3), "Maximum",     _fmt_bound(b, sizeof(b), p, p.max));
            _panel_field((uint16_t)(r + 4), "Description", p.desc);
            _panel_field((uint16_t)(r + 5), "Notes",       p.notes);
            _blank_line((uint16_t)(r + 6));
        }

        // Bottom prompt row: inline edit field or hint
        // --------------------------------------------------------------------

        /** @brief " name = " static label for the parameter being edited. */
        uint16_t _edit_label(char* buf, size_t cap) const {
            int n = snprintf(buf, cap, " %s = ", _edit_param->name);
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
            const char* hint = " UP/DOWN select   ENTER edit   ESC cancel";
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
            if (_selected >= _param_count()) return;
            _edit_param = &_config->param((uint8_t)_selected);
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
            int n = _config->format((uint8_t)_selected, val, sizeof(val));
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
            if (_config->set(*_edit_param, line)) {
                _editing = false;
                _dirty = true;        // value diff repaints the table row next tick
                _draw_prompt_row();
            } else {
                _edit_error = true;   // stay in edit, flag it
                _draw_error_marker();
            }
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

#endif // PAGES_CONFIG_PAGE_H
