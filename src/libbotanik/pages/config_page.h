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
#include "libbotanik/console/console.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/console/editor.h"

namespace Botanik { namespace Pages {

    /** @brief Full-screen editable configuration table (see file header). */
    class ConfigPage : public Page {
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
        static constexpr uint16_t PAINT_IDLE  = 0xFFFF;
        /** @} */

        /** @name State
         *  @{ */
        Console&            _console;
        AnsiTerminal&       _terminal;
        Cfg::ConfigHandler* _config;
        bool                _active   = false;
        /** @brief Staged-paint cursor (PAINT_IDLE when settled), advanced a few
         *  elements per tick under the TX floor so a full repaint spreads over
         *  ticks and never starves the status / alarm bars (as AlarmsPage). */
        uint16_t            _paint_i  = PAINT_IDLE;
        uint16_t            _selected  = 0;          ///< cursor row (param index)
        uint16_t            _sel_prev  = 0xFFFF;      ///< last-painted selection
        /** @brief Per-row value cache (diff gate; only changed rows repaint).
         *  64 rows is well above the library table; a site adding more than that
         *  many params would simply lose the diff gate on the overflow. */
        static constexpr uint16_t MAX_CACHE_ROWS = 64;
        char                _vcache[MAX_CACHE_ROWS][VAL_CAP];
        uint16_t            _cached_rows = 0;

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
            : _console(console), _terminal(console.terminal()), _config(config),
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
            uint16_t n = _param_count();
            if (n == 0) return;
            switch (cmd) {
                case Command::UP:
                case Command::CTRL_P:
                    if (_selected > 0) _selected--;
                    break;
                case Command::DOWN:
                case Command::CTRL_N:
                    if (_selected + 1 < n) _selected++;
                    break;
                case Command::HOME:      _selected = 0;         break;
                case Command::END:       _selected = (uint16_t)(n - 1); break;
                case Command::ENTER:     _start_edit();         break;
                default: break;
            }
        }

        bool handle_cancel() override {
            if (_editing) { _cancel_edit(); return true; }   // Ctrl+C leaves edit
            return false;
        }

        /** @} */

        /** @name Focus & update
         *  @{ */

        void deactivate() override { _active = false; }

        void focus() override {
            _active = true;
            _editing = false;
            if (_selected >= _param_count()) _selected = 0;
            _request_repaint();      // switch_to cleared the screen; paint fresh
        }

        void tick(bool active) override {
            _active = active;
            if (!active) return;
            // A staged full repaint advances a few elements per tick under the
            // TX floor, so the status bar keeps updating (as AlarmsPage).
            if (_painting()) { _paint_step(); return; }
            // Settled: repaint only rows whose value or selection changed, then
            // re-park the live cursor onto the edit field.
            bool painted = _repaint_changes();
            if (_editing && painted) _terminal.move_to(_editor.cursor_screen_pos());
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

        /** @brief Visible parameter rows (the smaller of the table and the
         *  content height). */
        uint16_t _shown() const {
            uint16_t vis = _visible_rows();
            uint16_t n   = _param_count();
            return (n < vis) ? n : vis;
        }

        // Rendering
        // --------------------------------------------------------------------

        uint16_t _value_w() const {
            // margins + "| " + name + " | " + type + " | " + value + " |"
            uint16_t used = (uint16_t)(2 * MARGIN + 2 + NAME_W + 3 + TYPE_W + 3 + 2);
            uint16_t cols = _terminal.cols();
            uint16_t w = (cols > used) ? (uint16_t)(cols - used) : 8;
            if (w < 8) w = 8;
            if (w > LINE_CAP - 48) w = (uint16_t)(LINE_CAP - 48);   // keep row in LINE_CAP
            return w;
        }

        /** @brief Build the full "  | name | type | value |" row into @p out
         *  (MARGIN left margin embedded; clear_to_eol wipes the right margin).
         *  @return its length. */
        uint16_t _row_text(uint16_t idx, char* out, size_t cap) const {
            char val[VAL_CAP];
            const Cfg::ConfigParam& p = _config->param((uint8_t)idx);
            _config->format((uint8_t)idx, val, sizeof(val));
            int n = snprintf(out, cap, "%*s| %-*.*s | %-*.*s | %-*.*s |",
                             (int)MARGIN, "", NAME_W, NAME_W, p.name,
                             TYPE_W, TYPE_W, Cfg::type_name(p),
                             _value_w(), _value_w(), val);
            return (n < 0) ? 0 : (uint16_t)n;
        }

        /** @brief The name field's padded contents only, for the cheap selection
         *  patch (no border or padding). */
        uint16_t _name_field_text(uint16_t idx, char* buf, size_t cap) const {
            const Cfg::ConfigParam& p = _config->param((uint8_t)idx);
            int n = snprintf(buf, cap, "%-*.*s", NAME_W, NAME_W, p.name);
            return (n < 0) ? 0 : (uint16_t)n;
        }

        /** @brief Draw a full data row; only the selected row's name field
         *  inverts (whole-row invert is dropped to save TX on selection moves). */
        void _draw_row(uint16_t idx, uint16_t row, bool selected) {
            char line[LINE_CAP];
            uint16_t n = _row_text(idx, line, sizeof(line));
            _terminal.move_to({row, 0});
            _terminal.set_inverted(false);
            _terminal.write_chars(line, NAME_CELL_L);                 // margin + "| "
            if (selected) _terminal.set_inverted(true);
            _terminal.write_chars(line + NAME_CELL_L, (uint16_t)(NAME_CELL_R - NAME_CELL_L));
            if (selected) _terminal.set_inverted(false);
            if (n > NAME_CELL_R)
                _terminal.write_chars(line + NAME_CELL_R, (uint16_t)(n - NAME_CELL_R));
            _terminal.clear_to_eol();
        }

        /** @brief Repaint only the name field of a row (its highlight moved). */
        void _patch_name_cell(uint16_t idx, uint16_t row, bool selected) {
            char cell[NAME_W + 2];
            uint16_t n = _name_field_text(idx, cell, sizeof(cell));
            _terminal.move_to({row, NAME_CELL_L});
            if (selected) _terminal.set_inverted(true);
            _terminal.write_chars(cell, n);
            if (selected) _terminal.set_inverted(false);
        }

        void _draw_header() {
            char line[LINE_CAP];
            int n = snprintf(line, sizeof(line), "%*s| %-*.*s | %-*.*s | %-*.*s |",
                             (int)MARGIN, "", NAME_W, NAME_W, "PARAMETER",
                             TYPE_W, TYPE_W, "TYPE", _value_w(), _value_w(), "VALUE");
            _terminal.move_to({HEADER_ROW, 0});
            _terminal.set_inverted(false);
            _terminal.write_chars(line, (n < 0) ? 0 : (uint16_t)n);
            _terminal.clear_to_eol();
        }

        void _draw_rule() {
            char line[LINE_CAP];
            uint16_t vw = _value_w();
            size_t k = 0;
            for (uint16_t i = 0; i < MARGIN && k < sizeof(line); i++)       line[k++] = ' ';
            if (k < sizeof(line)) line[k++] = '+';
            for (uint16_t i = 0; i < NAME_W + 2 && k < sizeof(line); i++)   line[k++] = '-';
            if (k < sizeof(line)) line[k++] = '+';
            for (uint16_t i = 0; i < TYPE_W + 2 && k < sizeof(line); i++)   line[k++] = '-';
            if (k < sizeof(line)) line[k++] = '+';
            for (uint16_t i = 0; i < vw + 2 && k < sizeof(line); i++)       line[k++] = '-';
            if (k < sizeof(line)) line[k++] = '+';
            _terminal.move_to({RULE_ROW, 0});
            _terminal.set_inverted(false);
            _terminal.write_chars(line, (uint16_t)k);
            _terminal.clear_to_eol();
        }

        // Staged paint (paced, shares AlarmsPage's TX-floor cadence)
        // --------------------------------------------------------------------

        bool _painting() const { return _paint_i != PAINT_IDLE; }
        void _request_repaint() { _paint_i = 0; }

        /** @brief Paint one staged element: 0 = blank top margin, 1 = header,
         *  2 = rule, 3.. = data rows (each seeds its value cache), last = the
         *  bottom prompt row. Order/count must match _paint_step()'s total. */
        void _paint_element(uint16_t e, uint16_t shown) {
            if (e == 0) {
                if (!_editing) _terminal.set_cursor_visible(false);
                _terminal.move_to({TOP_ROW, 0});
                _terminal.set_inverted(false);
                _terminal.clear_to_eol();
            } else if (e == 1) {
                _draw_header();
            } else if (e == 2) {
                _draw_rule();
            } else if (e < (uint16_t)(3 + shown)) {
                uint16_t i = (uint16_t)(e - 3);
                _draw_row(i, (uint16_t)(DATA_TOP + i), i == _selected);
                _config->format((uint8_t)i, _vcache[i], VAL_CAP);
            } else {
                _draw_prompt_row();
            }
        }

        /** @brief Advance the staged repaint: at most TX_COLD_FIELDS_PER_TICK
         *  elements this tick, only while the ring is shallow. The parameter set
         *  is static, so no mid-paint snapshot is needed (unlike AlarmsPage). */
        void _paint_step() {
            uint16_t shown = _shown();
            uint16_t total = (uint16_t)(shown + 4);   // top + header + rule + data + prompt
            uint16_t budget = Config::TX_COLD_FIELDS_PER_TICK;
            while (_paint_i < total && budget > 0 && _terminal.tx_below_cold()) {
                _paint_element(_paint_i, shown);
                _paint_i++;
                budget--;
            }
            if (_paint_i >= total) {
                _cached_rows = shown;
                _sel_prev = _selected;
                _paint_i = PAINT_IDLE;
            }
        }

        /** @brief Repaint only rows whose value or selection changed. A value
         *  change redraws the row; a selection move only patches the first cell.
         *  @return true if any row was painted (the caller re-parks the cursor). */
        bool _repaint_changes() {
            uint16_t shown = _shown();
            char val[VAL_CAP];
            bool painted = false;
            for (uint16_t i = 0; i < shown; i++) {
                _config->format((uint8_t)i, val, sizeof(val));
                bool sel_now = (i == _selected), sel_then = (i == _sel_prev);
                if (strcmp(val, _vcache[i]) != 0) {
                    _draw_row(i, (uint16_t)(DATA_TOP + i), sel_now);
                    memcpy(_vcache[i], val, VAL_CAP);
                    painted = true;
                } else if (sel_now != sel_then) {
                    _patch_name_cell(i, (uint16_t)(DATA_TOP + i), sel_now);
                    painted = true;
                }
            }
            _sel_prev = _selected;
            return painted;
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

        /** @brief Draw the bottom prompt row for the current state: the edit
         *  label + editor (+ any error marker) when editing, else the hint. */
        void _draw_prompt_row() {
            uint16_t row = (uint16_t)(_console.rows() - 1);
            _terminal.set_inverted(false);
            if (_editing) {
                _terminal.move_to({row, 0});
                _terminal.clear_to_eol();
                _draw_label();
                _editor.redraw();
                if (_edit_error) _draw_error_marker();
                _terminal.set_cursor_visible(true);
            } else {
                _terminal.set_cursor_visible(false);
                _terminal.move_to({row, 0});
                const char* hint = " UP/DOWN select   ENTER edit   ESC cancel";
                _terminal.write_chars(hint, (uint16_t)strlen(hint));
                _terminal.clear_to_eol();
            }
        }

        /** @brief Draw the "[invalid]" marker after the edit value, then restore
         *  the cursor to the edit position. */
        void _draw_error_marker() {
            _terminal.move_to(_editor.end_pos());
            const char* e = "  [invalid]";
            _terminal.write_chars(e, (uint16_t)strlen(e));
            _terminal.move_to(_editor.cursor_screen_pos());
        }

        /** @brief Clear a shown error marker (on the next edit keystroke). */
        void _clear_error() {
            _terminal.move_to(_editor.end_pos());
            _terminal.clear_to_eol();
            _edit_error = false;
            _terminal.move_to(_editor.cursor_screen_pos());
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
            else       _terminal.move_to(_editor.cursor_screen_pos());

            _edit_error = false;
            _editing = true;
            _terminal.set_cursor_visible(true);
        }

        void _cancel_edit() {
            _editing = false;
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
