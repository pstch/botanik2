// REVIEW_35: TODO
#ifndef PAGES_CONFIG_PAGE_H
#define PAGES_CONFIG_PAGE_H

/**
 * @file config_page.h
 * @brief ConfigPage - interactive table of configuration parameters.
 *
 * Layout (row 0 is the terminal status bar, the bottom row the alarm bar):
 *   row 1            header  (| PARAMETER | VALUE |)
 *   row 2            top rule
 *   rows 3..         one row per ConfigParam (name | current value)
 *   rows()-1         edit prompt / hint line (just above the alarm bar)
 *
 * UP/DOWN move a selection cursor (the selected row is drawn inverted);
 * ENTER opens an inline edit on the bottom prompt line, seeded with the row's
 * current value. Typing edits it; ENTER commits (parse + validate through
 * ConfigHandler::set, which persists + bumps the group serial); ESC/Ctrl+C
 * cancels. An invalid value is rejected and flagged, leaving edit mode open.
 * Rows repaint only on a change (value diff, selection move, edit state), so a
 * steady screen spends no TX budget.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "libbotanik_config.h"
#include "libbotanik/core/config.h"
#include "libbotanik/pages/page.h"
#include "libbotanik/console/console.h"
#include "libbotanik/console/terminal.h"

namespace Botanik { namespace Pages {

    /** @brief Full-screen editable configuration table (see file header). */
    class ConfigPage : public Page {
    private:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Console      = Terminal::Console;
        using Command      = Terminal::InputEvent::Command;

        /** @name Layout
         *  @{ */
        static constexpr uint16_t HEADER_ROW = 1;
        static constexpr uint16_t RULE_ROW   = 2;
        static constexpr uint16_t DATA_TOP   = 3;
        static constexpr uint16_t NAME_W     = 18;   // parameter-name column
        static constexpr uint16_t EDIT_CAP   = 96;   // edit buffer bytes
        /** @} */

        /** @name State
         *  @{ */
        Console&            _console;
        AnsiTerminal&       _terminal;
        Cfg::ConfigHandler* _config;
        bool                _active   = false;
        bool                _need_full = true;       ///< force a full repaint
        uint16_t            _selected  = 0;          ///< cursor row (param index)
        uint16_t            _sel_prev  = 0xFFFF;      ///< last-painted selection
        /** @brief Per-row value cache (diff gate; only changed rows repaint).
         *  64 rows is well above the library table; a site adding more than that
         *  many params would simply lose the diff gate on the overflow. */
        static constexpr uint16_t MAX_CACHE_ROWS = 64;
        char                _vcache[MAX_CACHE_ROWS][40];
        uint16_t            _cached_rows = 0;

        /** @name Edit sub-state
         *  @{ */
        bool                _editing   = false;
        const Cfg::ConfigParam* _edit_param = nullptr;
        char                _edit_buf[EDIT_CAP];
        uint16_t            _edit_len  = 0;
        uint16_t            _edit_pos  = 0;
        bool                _edit_error = false;
        bool                _prompt_dirty = true;
        /** @} */
        /** @} */

    public:
        /** @brief @p console owns the content geometry; @p config is the backend
         *  the table reads/writes (may be null -> empty table). */
        ConfigPage(Console& console, Cfg::ConfigHandler* config)
            : _console(console), _terminal(console.terminal()), _config(config) {}

        const char* name() const override { return "CONFIG"; }

        /** @name Input
         *  @{ */

        void handle_text(char c) override {
            if (!_editing) return;
            if (c < 0x20 || c == 0x7F) return;           // printable only
            if (_edit_len + 1 >= EDIT_CAP) return;
            // insert at the cursor
            for (uint16_t i = _edit_len; i > _edit_pos; i--) _edit_buf[i] = _edit_buf[i - 1];
            _edit_buf[_edit_pos++] = c;
            _edit_len++;
            _edit_buf[_edit_len] = 0;
            _edit_error = false;
            _prompt_dirty = true;
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
            _need_full = true;
            _editing = false;
            _prompt_dirty = true;
            if (_selected >= _param_count()) _selected = 0;
        }

        void tick(bool active) override {
            _active = active;
            if (!active) return;
            if (_need_full) { _draw_all(); return; }
            _repaint_changes();
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

        // Rendering

        /** @brief Build the fixed-width "| name | value |" row into @p out. */
        void _row_text(uint16_t idx, char* out, size_t cap) const {
            char val[40];
            const Cfg::ConfigParam& p = _config->param((uint8_t)idx);
            _config->format((uint8_t)idx, val, sizeof(val));
            uint16_t vw = _value_w();
            snprintf(out, cap, "| %-*.*s | %-*.*s |",
                     NAME_W, NAME_W, p.name, vw, vw, val);
        }

        uint16_t _value_w() const {
            // "| " + name + " | " + value + " |" == cols
            uint16_t used = (uint16_t)(2 + NAME_W + 3 + 2);
            uint16_t w = (_terminal.cols() > used) ? (uint16_t)(_terminal.cols() - used) : 8;
            return (w > 36) ? 36 : w;   // cache column width bound
        }

        void _draw_row(uint16_t idx, uint16_t row, bool selected) {
            char line[128];
            _row_text(idx, line, sizeof(line));
            _terminal.move_to({row, 0});
            _terminal.force_inverted(selected);
            _terminal.write_chars(line, (uint16_t)strlen(line));
            _terminal.set_inverted(false);
            _terminal.clear_to_eol();
        }

        void _draw_all() {
            _terminal.set_cursor_visible(false);
            // Header + rule.
            char hdr[128];
            uint16_t vw = _value_w();
            snprintf(hdr, sizeof(hdr), "| %-*.*s | %-*.*s |",
                     NAME_W, NAME_W, "PARAMETER", vw, vw, "VALUE");
            _terminal.move_to({HEADER_ROW, 0});
            _terminal.write_chars(hdr, (uint16_t)strlen(hdr));
            _terminal.clear_to_eol();

            char rule[128];
            size_t rn = 0;
            rule[rn++] = '+';
            for (int i = 0; i < NAME_W + 2 && rn < sizeof(rule) - 1; i++) rule[rn++] = '-';
            rule[rn++] = '+';
            for (int i = 0; i < vw + 2 && rn < sizeof(rule) - 1; i++) rule[rn++] = '-';
            rule[rn++] = '+';
            rule[rn] = 0;
            _terminal.move_to({RULE_ROW, 0});
            _terminal.write_chars(rule, (uint16_t)rn);
            _terminal.clear_to_eol();

            // Data rows (cache each value).
            uint16_t n = _param_count();
            uint16_t vis = _visible_rows();
            uint16_t shown = (n < vis) ? n : vis;
            for (uint16_t i = 0; i < shown; i++) {
                _draw_row(i, (uint16_t)(DATA_TOP + i), i == _selected);
                _config->format((uint8_t)i, _vcache[i], sizeof(_vcache[0]));
            }
            // Clear any rows a previous (larger) table left behind.
            for (uint16_t r = (uint16_t)(DATA_TOP + shown); r < (uint16_t)(_console.rows() - 1); r++) {
                _terminal.move_to({r, 0});
                _terminal.clear_to_eol();
            }
            _cached_rows = shown;
            _sel_prev = _selected;
            _need_full = false;
            _prompt_dirty = true;
            _draw_prompt();
        }

        /** @brief Repaint only rows whose value or selection changed. */
        void _repaint_changes() {
            uint16_t vis = _visible_rows();
            uint16_t n = _param_count();
            uint16_t shown = (n < vis) ? n : vis;
            char val[40];
            for (uint16_t i = 0; i < shown; i++) {
                _config->format((uint8_t)i, val, sizeof(val));
                bool sel_now  = (i == _selected);
                bool sel_then = (i == _sel_prev);
                bool changed  = (strcmp(val, _vcache[i]) != 0) || (sel_now != sel_then);
                if (!changed) continue;
                _draw_row(i, (uint16_t)(DATA_TOP + i), sel_now);
                memcpy(_vcache[i], val, sizeof(_vcache[0]));
            }
            _sel_prev = _selected;
            if (_prompt_dirty) _draw_prompt();
        }

        void _draw_prompt() {
            uint16_t row = (uint16_t)(_console.rows() - 1);
            _terminal.move_to({row, 0});
            _terminal.set_inverted(false);
            if (_editing) {
                char line[160];
                int n = snprintf(line, sizeof(line), " %s = %s", _edit_param->name, _edit_buf);
                _terminal.write_chars(line, (n < 0) ? 0 : (uint16_t)n);
                if (_edit_error) {
                    const char* e = "   [invalid]";
                    _terminal.write_chars(e, (uint16_t)strlen(e));
                }
                _terminal.clear_to_eol();
                // Park the cursor at the edit position.
                uint16_t base = (uint16_t)(1 + strlen(_edit_param->name) + 3);
                _terminal.move_to({row, (uint16_t)(base + _edit_pos)});
                _terminal.set_cursor_visible(true);
            } else {
                _terminal.set_cursor_visible(false);
                const char* hint = " UP/DOWN select   ENTER edit   ESC cancel";
                _terminal.write_chars(hint, (uint16_t)strlen(hint));
                _terminal.clear_to_eol();
            }
            _prompt_dirty = false;
        }

        // Edit sub-state

        void _start_edit() {
            if (_selected >= _param_count()) return;
            _edit_param = &_config->param((uint8_t)_selected);
            int n = _config->format((uint8_t)_selected, _edit_buf, sizeof(_edit_buf));
            _edit_len = (n < 0) ? 0 : (uint16_t)n;
            _edit_pos = _edit_len;
            _edit_error = false;
            _editing = true;
            _prompt_dirty = true;
        }

        void _cancel_edit() {
            _editing = false;
            _prompt_dirty = true;
        }

        void _commit_edit() {
            _edit_buf[_edit_len] = 0;
            if (_config->set(*_edit_param, _edit_buf)) {
                _editing = false;
                _prompt_dirty = true;      // value diff repaints the row next tick
            } else {
                _edit_error = true;        // stay in edit, flag it
                _prompt_dirty = true;
            }
        }

        void _handle_edit_command(Command cmd, uint16_t count) {
            switch (cmd) {
                case Command::ENTER:      _commit_edit(); break;
                case Command::ESC:        _cancel_edit(); break;
                case Command::BACKSPACE:
                    for (uint16_t k = 0; k < count && _edit_pos > 0; k++) {
                        for (uint16_t i = (uint16_t)(_edit_pos - 1); i < _edit_len; i++)
                            _edit_buf[i] = _edit_buf[i + 1];
                        _edit_pos--; _edit_len--;
                    }
                    _edit_error = false; _prompt_dirty = true;
                    break;
                case Command::DELETE:
                case Command::CTRL_D:
                    for (uint16_t k = 0; k < count && _edit_pos < _edit_len; k++) {
                        for (uint16_t i = _edit_pos; i < _edit_len; i++)
                            _edit_buf[i] = _edit_buf[i + 1];
                        _edit_len--;
                    }
                    _edit_error = false; _prompt_dirty = true;
                    break;
                case Command::LEFT:  case Command::CTRL_B:
                    if (_edit_pos > 0) { _edit_pos--; _prompt_dirty = true; } break;
                case Command::RIGHT: case Command::CTRL_F:
                    if (_edit_pos < _edit_len) { _edit_pos++; _prompt_dirty = true; } break;
                case Command::HOME:  case Command::CTRL_A:
                    _edit_pos = 0; _prompt_dirty = true; break;
                case Command::END:   case Command::CTRL_E:
                    _edit_pos = _edit_len; _prompt_dirty = true; break;
                case Command::CTRL_U:
                    _edit_len = 0; _edit_pos = 0; _edit_buf[0] = 0;
                    _edit_error = false; _prompt_dirty = true; break;
                default: break;
            }
        }
    };

}}

#endif // PAGES_CONFIG_PAGE_H
