/**
 * @file scroll_table.cpp
 * @brief ScrollTablePage implementation - the scrolling table frame.
 */
#include "libbotanik/pages/scroll_table.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_CONSOLE

#include <cstdio>

#include "libbotanik/console/kill_ring.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/utils/text.h"

namespace Botanik { namespace Pages {

    void ScrollTablePage::_clamp_selection() {
        uint16_t n = _row_count();
        if (n == 0)              _selected = 0;
        else if (_selected >= n) _selected = (uint16_t)(n - 1);
    }

    void ScrollTablePage::_follow_selection() {
        uint16_t win = _window();
        if (win == 0) return;
        if (_selected < _top)             _top = _selected;
        else if (_selected >= _top + win) _top = (uint16_t)(_selected - win + 1);
        uint16_t maxtop = _max_top();
        if (_top > maxtop) _top = maxtop;
    }

    bool ScrollTablePage::_nav_command(Command cmd) {
        switch (cmd) {
            case Command::UP:   case Command::CTRL_P: _move_selection(-1); return true;
            case Command::DOWN: case Command::CTRL_N: _move_selection(+1); return true;
            case Command::PAGE_UP:   _move_selection(-(int)_page_step()); return true;
            case Command::PAGE_DOWN: _move_selection(+(int)_page_step()); return true;
            case Command::HOME: _move_selection(-(int)_row_count()); return true;
            case Command::END:  _move_selection(+(int)_row_count()); return true;
            case Command::ALT_W: _copy_selected_cell();              return true;
            default: return false;
        }
    }

    void ScrollTablePage::_copy_selected_cell() {
        const char* cell = _selected_cell();
        if (cell == nullptr || cell[0] == '\0') return;
        if (_console.kill_ring().set(cell, (uint16_t)strlen(cell)) == 0) return;

        // Say so: the table looks identical after a copy, so without the badge
        // the only way to know the key landed is to go and paste it.
        snprintf(_copied, sizeof(_copied), "copied %s", cell);
        _copied_until_ms = HAL::Clock::uptime_ms() + COPIED_BADGE_MS;
    }

    const char* ScrollTablePage::_copied_badge() const {
        return HAL::Clock::uptime_ms() < _copied_until_ms ? _copied : "";
    }

    void ScrollTablePage::_move_selection(int dir) {
        uint16_t n = _row_count();
        if (n == 0) return;
        int32_t s = (int32_t)_selected + dir;
        if (s < 0)           s = 0;
        if (s >= (int32_t)n) s = (int32_t)n - 1;
        _selected = (uint16_t)s;
        uint16_t old_top = _top;
        _follow_selection();
        int32_t td = (int32_t)_top - (int32_t)old_top;
        if (td == 0) return;
        uint16_t win = _window();
        uint16_t nd  = (uint16_t)(td < 0 ? -td : td);
        if (nd < win) {
            uint16_t top = _data_top();
            _terminal.scroll_region(top, (uint16_t)(top + win - 1),
                                    /*up=*/td > 0, nd);
        }
    }

    void ScrollTablePage::_cycle_mode(int dir) {
        uint8_t n = _mode_count();
        if (n <= 1) return;
        _mode     = (uint8_t)(((int)_mode + dir + (int)n) % (int)n);
        _top      = 0;
        _selected = 0;
        _on_mode_changed();
    }

    void ScrollTablePage::_draw_legend(uint16_t row, const char* detail) {
        char buf[Config::TABLE_LINE_LEN], field[Config::TABLE_FILTER_LEN + 8];
        uint16_t w = 0;
        int n = 0;
        // Where reverse video starts and stops: the view badge, the filter, or
        // both - a table can carry either.
        struct { uint16_t at, end; } inv[2];
        uint8_t ninv = 0;

        const uint8_t n_modes = _mode_count();
        if (n_modes > 1) {
            snprintf(field, sizeof(field), "%s <",
                     _mode_label((uint8_t)((_mode + n_modes - 1) % n_modes)));
            _place_centered(buf, w, sizeof(buf), ML_PREV, ML_SLOT_W, field);

            snprintf(field, sizeof(field), "[%s]", _mode_name());
            const uint16_t at =
                _place_centered(buf, w, sizeof(buf), ML_BADGE, ML_SLOT_W, field);
            inv[ninv].at = at; inv[ninv].end = w; ninv++;

            snprintf(field, sizeof(field), "> %s",
                     _mode_label((uint8_t)((_mode + 1) % n_modes)));
            _place_centered(buf, w, sizeof(buf), ML_NEXT, ML_SLOT_W, field);
        }

        // How much of the set is on screen; every table shows it.
        _pad_gap(buf, w, sizeof(buf), ML_POS);
        const uint16_t total = _row_count(), win = _window();
        const uint16_t last  = (uint16_t)(_top + (win < total ? win : total));
        n = snprintf(buf + w, sizeof(buf) - w, "%u-%u / %u",
                     (unsigned)(total ? _top + 1 : 0), (unsigned)last,
                     (unsigned)total);
        if (n > 0) w = (uint16_t)(w + n);

        // The filter itself, not a count of what it kept: what the table is
        // showing and what was typed are then one thing, and cannot disagree.
        if (_filter_n != 0) {
            _pad_gap(buf, w, sizeof(buf), ML_FILTER);
            const uint16_t at = w;
            n = snprintf(buf + w, sizeof(buf) - w, "[/%s]", _filter);
            if (n > 0) w = (uint16_t)(w + n);
            inv[ninv].at = at; inv[ninv].end = w; ninv++;
        }

        // A fresh Alt+W borrows the detail slot. The page's own detail is a
        // standing hint and comes back in a few seconds; the copy is news.
        const char* copied = _copied_badge();
        if (copied[0] != '\0') detail = copied;
        // Nothing on screen says a table can be filtered at all, so the hint
        // holds the slot until there IS a filter, or the page has something of
        // its own to say.
        else if ((detail == nullptr || detail[0] == '\0') && _filter_n == 0)
            detail = "type to filter";

        if (detail != nullptr && detail[0] != '\0') {
            _pad_gap(buf, w, sizeof(buf), ML_DETAIL);
            n = snprintf(buf + w, sizeof(buf) - w, "%s", detail);
            if (n > 0) w = (uint16_t)(w + n);
        }
        if (w > sizeof(buf) - 1) w = (uint16_t)(sizeof(buf) - 1);

        // One pass: plain, inverted through each badge, plain after.
        _terminal.move_to({row, 0});
        _terminal.set_inverted(false);
        uint16_t at = 0;
        for (uint8_t i = 0; i < ninv; i++) {
            _terminal.write_chars(buf + at, (uint16_t)(inv[i].at - at));
            _terminal.set_inverted(true);
            _terminal.write_chars(buf + inv[i].at, (uint16_t)(inv[i].end - inv[i].at));
            _terminal.set_inverted(false);
            at = inv[i].end;
        }
        _terminal.write_chars(buf + at, (uint16_t)(w - at));
        _terminal.clear_to_eol();
    }

    bool ScrollTablePage::_filter_match(const char* s) const {
        if (_filter_n == 0) return true;
        return Utils::contains_ci(s, _filter);
    }

    bool ScrollTablePage::_filter_text(char c) {
        if (c < 0x20 || c == 0x7F) return false;
        if (c == ' ' && _filter_n == 0) return true;   // no name holds one
        if (_filter_n < Config::TABLE_FILTER_LEN) {
            _filter[_filter_n++] = c;
            _filter[_filter_n] = '\0';
            _filter_changed();
        }
        return true;
    }

    bool ScrollTablePage::_filter_command(Command cmd) {
        if (cmd == Command::BACKSPACE) {
            if (_filter_n != 0) { _filter[--_filter_n] = '\0'; _filter_changed(); }
            return true;
        }
        if (cmd == Command::ESC) {
            if (_filter_n != 0) { _filter_n = 0; _filter[0] = '\0'; _filter_changed(); }
            return true;
        }
        return false;
    }

    void ScrollTablePage::_filter_changed() {
        // Back to the top: the row the cursor sat on is not the row that index
        // names in the narrowed set.
        _top      = 0;
        _selected = 0;
        if (++_filter_gen == 0) _filter_gen = 1;   // 0 is "never built"
        _on_filter_changed();
    }

    bool ScrollTablePage::_mode_command(Command cmd) {
        if (_mode_count() <= 1) return false;
        if (cmd == Command::RIGHT) { _cycle_mode(+1); return true; }
        if (cmd == Command::LEFT)  { _cycle_mode(-1); return true; }
        return false;
    }

    ScrollTablePage::EditKey ScrollTablePage::_edit_kind(Command cmd) {
        switch (cmd) {
            case Command::BACKSPACE: case Command::DELETE: case Command::CTRL_D:
            case Command::CTRL_U:    case Command::CTRL_K: case Command::CTRL_W:
            case Command::CTRL_Y:
                return EDIT_CHANGED;
            case Command::LEFT:  case Command::CTRL_B:
            case Command::RIGHT: case Command::CTRL_F:
            case Command::HOME:  case Command::CTRL_A:
            case Command::END:   case Command::CTRL_E:
            case Command::CTRL_SPACE: case Command::CTRL_G: case Command::ALT_W:
                return EDIT_MOVED;
            default:
                return EDIT_IGNORED;
        }
    }

    void ScrollTablePage::_edit_apply(Terminal::TextEditor& ed, Command cmd,
                                      uint16_t count) {
        switch (cmd) {
            case Command::BACKSPACE:  ed.backspace(count);            break;
            case Command::DELETE:
            case Command::CTRL_D:     ed.delete_forward(count);       break;
            case Command::LEFT:
            case Command::CTRL_B:     ed.move_left();                 break;
            case Command::RIGHT:
            case Command::CTRL_F:     ed.move_right();                break;
            case Command::HOME:
            case Command::CTRL_A:     ed.move_to_line_start();        break;
            case Command::END:
            case Command::CTRL_E:     ed.move_to_line_end();          break;
            case Command::CTRL_U:
                ed.move_to_line_start();
                ed.delete_forward(ed.buffer_len());                   break;
            // The same cut/paste as the shell prompt, over the same ring: Alt+W
            // on the table puts a name in it, and this is where it lands.
            case Command::CTRL_K:     ed.kill_to_line_end();          break;
            case Command::CTRL_W:     ed.kill_region_or_word();       break;
            case Command::CTRL_SPACE: ed.toggle_mark();               break;
            case Command::CTRL_G:     ed.clear_mark();                break;
            case Command::ALT_W:      ed.copy_region();               break;
            case Command::CTRL_Y:     ed.yank();                      break;
            default: break;
        }
    }

    void ScrollTablePage::_draw_line(uint16_t row, const char* line, uint16_t n) {
        _terminal.move_to({row, 0});
        _terminal.set_inverted(false);
        _terminal.write_chars(line, n);
        _terminal.clear_to_eol();
    }

    uint8_t ScrollTablePage::_fit_columns(Col* out) const {
        const uint8_t n = _columns(out);
        if (n == 0) return 0;
        // What the cells may span: the display less both margins and the
        // closing rule. _build_row() lays out exactly that.
        const uint16_t cols = _terminal.cols();
        const uint16_t room = (cols > (uint16_t)(2 * MARGIN + 1))
                            ? (uint16_t)(cols - 2 * MARGIN - 1) : 0;

        const uint8_t f = (_flex_col() < n) ? _flex_col() : 0;
        uint16_t used = _cell_pitch(0);               // the flex cell's own borders
        for (uint8_t i = 0; i < n; i++)
            if (i != f) used = (uint16_t)(used + _cell_pitch(out[i].width));
        uint16_t w = (room > used) ? (uint16_t)(room - used) : 0;
        out[f].width = (w < FLEX_MIN) ? FLEX_MIN : w;

        // Still too wide: the fixed columns alone overrun the display. Shave the
        // widest until the row fits, rather than letting the terminal clip it
        // mid-cell and lose the closing rule. `used` already holds every other
        // cell's pitch, and pitch is width plus a constant, so the flex cell's
        // width is all that is left to add.
        used = (uint16_t)(used + out[f].width);
        while (used > room) {
            uint8_t big = 0;
            for (uint8_t i = 1; i < n; i++) if (out[i].width > out[big].width) big = i;
            if (out[big].width <= MIN_COL_W) break;   // nothing left to give
            out[big].width--;
            used--;
        }
        return n;
    }

    void ScrollTablePage::_cell_span(uint8_t i, uint16_t& start, uint16_t& end) const {
        Col cols[MAX_COLS];
        uint8_t n = _fit_columns(cols);
        start = (uint16_t)(MARGIN + 2);               // margin + "| "
        for (uint8_t j = 0; j < i && j < n; j++)
            start = (uint16_t)(start + _cell_pitch(cols[j].width));
        end = (uint16_t)(start + (i < n ? cols[i].width : 0));
    }

    uint16_t ScrollTablePage::_build_row(char* out, size_t cap, const char* const* cells) const {
        Col cols[MAX_COLS];
        uint8_t n = _fit_columns(cols);
        const bool pad = _pad_cells();
        uint16_t w = 0;
        for (uint16_t i = 0; i < MARGIN && w < cap - 1; i++) out[w++] = ' ';
        for (uint8_t c = 0; c < n; c++) {
            if (w < cap - 1) out[w++] = '|';
            const int cw = (int)cols[c].width;
            int m = snprintf(out + w, cap - w, pad ? " %-*.*s " : " %-*.*s",
                             cw, cw, cells[c] != nullptr ? cells[c] : "");
            if (m < 0) m = 0;
            w = (uint16_t)(w + m);
            if (w > cap - 1) { w = (uint16_t)(cap - 1); break; }
        }
        if (w < cap - 1) out[w++] = '|';               // closing rule
        return w;
    }

    void ScrollTablePage::_draw_header(uint16_t row) {
        Col cols[MAX_COLS];
        uint8_t n = _fit_columns(cols);
        const char* heads[MAX_COLS];
        for (uint8_t i = 0; i < n; i++) heads[i] = cols[i].head;
        char line[Config::TABLE_LINE_LEN];
        _draw_line(row, line, _build_row(line, sizeof(line), heads));
    }

    void ScrollTablePage::_draw_rule(uint16_t row, char cap, char join) {
        Col cols[MAX_COLS];
        uint8_t n = _fit_columns(cols);
        char line[Config::TABLE_LINE_LEN];
        uint16_t w = 0;
        for (uint16_t i = 0; i < MARGIN && w < sizeof(line) - 1; i++) line[w++] = ' ';
        if (w < sizeof(line) - 1) line[w++] = cap;
        for (uint8_t c = 0; c < n; c++) {
            // The rule spans the whole cell: its contents plus the padding
            // the row builder puts either side.
            uint16_t span = (uint16_t)(_cell_pitch(cols[c].width) - 1);
            for (uint16_t k = 0; k < span && w < sizeof(line) - 1; k++) line[w++] = '-';
            if (w < sizeof(line) - 1) line[w++] = (c + 1 == n) ? cap : join;
        }
        _draw_line(row, line, w);
    }

    void ScrollTablePage::_draw_separator(uint16_t row) {
        char buf[Config::TABLE_LINE_LEN];
        uint16_t n = 0;
        for (uint16_t i = 0; i < MARGIN && n < sizeof(buf) - 1; i++) buf[n++] = ' ';
        uint16_t cols = _terminal.cols();
        uint16_t w = (cols > (uint16_t)(2 * MARGIN)) ? (uint16_t)(cols - 2 * MARGIN) : 0;
        for (uint16_t i = 0; i < w && n < sizeof(buf) - 1; i++) buf[n++] = '-';
        _draw_line(row, buf, n);
    }

    void ScrollTablePage::_panel_field(uint16_t row, const char* label, const char* value) {
        char buf[Config::TABLE_LINE_LEN];
        int n = snprintf(buf, sizeof(buf), " %-12s : %s", label,
                         (value && value[0]) ? value : "-");
        _draw_line(row, buf, (n < 0) ? 0 : (uint16_t)n);
    }
}}

#endif // BOTANIK_FEATURE_CONSOLE
