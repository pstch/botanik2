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
 *   row 2            header  (| PARAMETER | TYPE | VALUE |)
 *   row 3            top rule
 *   rows 4..         one row per ConfigParam (name | type | current value)
 *   rows()-1         edit prompt / hint line (just above the alarm bar)
 *
 * UP/DOWN move a selection cursor (the selected row is drawn inverted);
 * ENTER opens an inline edit on the bottom prompt line: the page draws a static
 * " name = " label and a single-line TextEditor (set_indent past the label,
 * set_single_line) renders the value after it, seeded with the row's current
 * text. Typing edits it through the editor's byte-efficient reconcile; ENTER
 * commits (parse + validate through ConfigHandler::set, which persists + bumps
 * the group serial); ESC/Ctrl+C cancels. An invalid value is rejected and
 * flagged, leaving edit mode open. Rows repaint only on a change (value diff,
 * selection move), so a steady screen spends no TX budget.
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
         *  margin row, MARGIN blank columns each side, widened to the display.
         *  Row 0 is the status bar; the bottom row is the edit/hint prompt. */
        static constexpr uint16_t MARGIN     = 2;    // blank columns each side
        static constexpr uint16_t TOP_ROW    = 1;    // blank top margin
        static constexpr uint16_t HEADER_ROW = 2;    // | PARAMETER | TYPE | VALUE |
        static constexpr uint16_t RULE_ROW   = 3;    // +----+...+
        static constexpr uint16_t DATA_TOP   = 4;    // first parameter row
        static constexpr uint16_t NAME_W     = 18;   // parameter-name column
        static constexpr uint16_t TYPE_W     = 6;    // data-type column
        static constexpr uint16_t VAL_CAP    = 64;   // formatted value + diff cache
        static constexpr uint16_t LINE_CAP   = 192;  // row string buffer
        // Selected-row highlight covers only the name field's contents (not its
        // borders or padding), and only the first cell: a selection move
        // repaints two small fields, not two rows.
        static constexpr uint16_t NAME_CELL_L = MARGIN + 2;            // "| " skipped
        static constexpr uint16_t NAME_CELL_R = MARGIN + 2 + NAME_W;   // name field end
        /** @} */

        /** @name State
         *  @{ */
        Console&            _console;
        Cfg::ConfigHandler* _config;
        bool                _dirty    = true;    ///< table needs a redraw (nav / edit / focus)
        uint64_t            _last_refresh_ms = 0; ///< last cadence rebuild

        /** @name Edit sub-state
         *  @{ */
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
            // While editing, the editor owns the screen (it redraws the prompt row
            // and its cursor on each keystroke); leave the static table as drawn.
            if (_editing) return;
            // Rebuild on nav/edit (_dirty) or the value cadence; the cadence still
            // catches an external group-serial bump. Re-formatting every row every
            // tick would burn CPU for a screen that almost never changes (monitor
            // gates the same way).
            uint64_t now = HAL::Clock::uptime_ms();
            if (!_dirty && now - _last_refresh_ms < Config::PAGE_HOT_MS) return;
            _last_refresh_ms = now;
            _dirty = false;
            // Redraw the table immediate-mode; the terminal's back-buffer diff emits
            // only the cells that moved, so the page keeps no per-row cache.
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
            _draw_prompt_row();
        }

        /** @} */

    private:
        uint16_t _param_count() const { return _config != nullptr ? _config->count() : 0; }

        /** @brief Rows available for parameters (header + rule above, prompt
         *  line below). */
        uint16_t _visible_rows() const {
            uint16_t prompt = (uint16_t)(_console.rows() - 1);
            return (prompt > DATA_TOP) ? (uint16_t)(prompt - DATA_TOP) : 0;
        }

        /** @name ScrollTablePage geometry
         *  @{ */
        uint16_t _data_top()  const override { return DATA_TOP; }
        uint16_t _window()    const override { return _visible_rows(); }
        uint16_t _row_count() const override { return _param_count(); }
        /** @} */

        // Rendering
        // --------------------------------------------------------------------

        uint16_t _value_w() const {
            return _value_width(_terminal.cols(), MARGIN, NAME_W, TYPE_W,
                                (uint16_t)(LINE_CAP - 48));   // keep row in LINE_CAP
        }

        /** @brief Build the full "  | name | type | value |" row into @p out
         *  (MARGIN left margin embedded; clear_to_eol wipes the right margin).
         *  @return its length. */
        uint16_t _row_text(uint16_t idx, char* out, size_t cap) const {
            char val[VAL_CAP];
            const Cfg::ConfigParam& p = _config->param((uint8_t)idx);
            _config->format((uint8_t)idx, val, sizeof(val));
            return _bordered_row(out, cap, MARGIN, NAME_W, p.name,
                                 TYPE_W, Cfg::type_name(p), _value_w(), val);
        }

        /** @brief Draw a full data row @p idx at screen @p row; only the selected
         *  row's NAME field inverts. The terminal's back-buffer diff pares it to the
         *  cells that actually moved. */
        void _draw_row(uint16_t idx, uint16_t row, bool selected) {
            char line[LINE_CAP];
            uint16_t n = _row_text(idx, line, sizeof(line));
            _draw_selected_line(row, line, n, NAME_CELL_L, NAME_CELL_R, selected);
        }

        void _draw_header() {
            char line[LINE_CAP];
            uint16_t n = _bordered_row(line, sizeof(line), MARGIN,
                                       NAME_W, "PARAMETER", TYPE_W, "TYPE",
                                       _value_w(), "VALUE");
            _draw_line(HEADER_ROW, line, n);
        }

        void _draw_rule() {
            uint16_t spans[3] = { (uint16_t)(NAME_W + 2), (uint16_t)(TYPE_W + 2),
                                  (uint16_t)(_value_w() + 2) };
            ScrollTablePage::_draw_rule(RULE_ROW, MARGIN, spans, 3, '+', '+');
        }

        /** @brief Draw the static chrome: top margin, header, rule. */
        void _draw_structure() {
            _blank_line(TOP_ROW);
            _draw_header();
            _draw_rule();
        }

        // Prompt row (bottom): edit field or hint
        // --------------------------------------------------------------------

        /** @brief " name = " static label for the parameter being edited; the
         *  editor's indent starts one past it. @return the label width. */
        uint16_t _edit_label(char* buf, size_t cap) const {
            int n = snprintf(buf, cap, " %s = ", _edit_param->name);
            return (n < 0) ? 0 : (uint16_t)n;
        }

        /** @brief Draw the static label at column 0 of the prompt row. */
        void _draw_label() {
            char label[48];
            uint16_t lw = _edit_label(label, sizeof(label));
            _terminal.move_to({(uint16_t)(_console.rows() - 1), 0});
            _terminal.write_chars(label, lw);
        }

        /** @brief Draw the bottom prompt row for the current state. When editing,
         *  the editor owns the row; otherwise this paints the idle hint. */
        void _draw_prompt_row() {
            uint16_t row = (uint16_t)(_console.rows() - 1);
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

        /** @brief Draw the "[invalid]" marker after the edit value, then restore
         *  the cursor to the edit position. */
        void _draw_error_marker() {
            _terminal.move_to(_editor.end_pos());
            const char* e = "  [invalid]";
            _terminal.write_chars(e, (uint16_t)strlen(e));
            _editor.place_cursor();
        }

        /** @brief Clear a shown error marker (on the next edit keystroke). */
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
            uint16_t row = (uint16_t)(_console.rows() - 1);
            char label[48];
            uint16_t lw = _edit_label(label, sizeof(label));

            // Single-line editor, indented past the label the page draws.
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
                _dirty = true;
                _draw_prompt_row();   // value diff repaints the table row next tick
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
