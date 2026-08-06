/**
 * @file scroll_table.cpp
 * @brief ScrollTablePage implementation - the scrolling table frame.
 */
#include "libbotanik/pages/scroll_table.h"

#include <cstdio>                           // snprintf

#include "libbotanik/hal/platform.h"        // HAL::Clock::uptime_ms

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
            default: return false;
        }
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

    void ScrollTablePage::_draw_mode_line(uint16_t row, const char* group, const char* detail) {
        const uint8_t n_modes = _mode_count();
        const bool    in_all  = (group != nullptr && group[0] != '\0');
        char left[Config::TABLE_LINE_LEN], field[32];
        uint16_t w = 0;
        int n = 0;

        // Previous view. Omitted in an "all" view, which has no meaningful
        // "back" - it is where back leads.
        if (!in_all && n_modes > 1) {
            snprintf(field, sizeof(field), "%s <",
                     _mode_label((uint8_t)((_mode + n_modes - 1) % n_modes)));
            _place_centered(left, w, sizeof(left), ML_PREV, ML_SLOT_W, field);
        }

        snprintf(field, sizeof(field), "[%s]", _mode_name());
        const uint16_t badge_at =
            _place_centered(left, w, sizeof(left), ML_BADGE, ML_SLOT_W, field);
        const uint16_t badge_end = w;

        if (in_all) {
            snprintf(field, sizeof(field), "(%s)", group);
        } else if (n_modes > 1) {
            snprintf(field, sizeof(field), "> %s",
                     _mode_label((uint8_t)((_mode + 1) % n_modes)));
        } else {
            field[0] = '\0';
        }
        if (field[0] != '\0')
            _place_centered(left, w, sizeof(left), ML_NEXT, ML_SLOT_W, field);

        // How much of the set is on screen - every table shows it, since
        // "am I looking at all of this?" is the same question everywhere.
        _pad_to(left, w, sizeof(left), ML_POS);
        const uint16_t total = _row_count(), win = _window();
        const uint16_t last  = (uint16_t)(_top + (win < total ? win : total));
        n = snprintf(left + w, sizeof(left) - w, "%u-%u / %u",
                     (unsigned)(total ? _top + 1 : 0), (unsigned)last,
                     (unsigned)total);
        if (n > 0) w = (uint16_t)(w + n);

        if (detail != nullptr && detail[0] != '\0') {
            _pad_to(left, w, sizeof(left), ML_DETAIL);
            n = snprintf(left + w, sizeof(left) - w, "%s", detail);
            if (n > 0) w = (uint16_t)(w + n);
        }
        if (w > sizeof(left) - 1) w = (uint16_t)(sizeof(left) - 1);

        // One pass: plain up to the badge, inverted through it, plain after.
        _terminal.move_to({row, 0});
        _terminal.set_inverted(false);
        _terminal.write_chars(left, badge_at);
        _terminal.set_inverted(true);
        _terminal.write_chars(left + badge_at, (uint16_t)(badge_end - badge_at));
        _terminal.set_inverted(false);
        _terminal.write_chars(left + badge_end, (uint16_t)(w - badge_end));
        _terminal.clear_to_eol();
    }

    bool ScrollTablePage::_mode_command(Command cmd) {
        if (_mode_count() <= 1) return false;
        if (cmd == Command::RIGHT) { _cycle_mode(+1); return true; }
        if (cmd == Command::LEFT)  { _cycle_mode(-1); return true; }
        // Getting back to the whole set should not mean cycling through
        // every group to reach it.
        if (cmd == Command::BACKSPACE) {
            if (_mode != 0) _cycle_mode(-(int)_mode);
            return true;
        }
        return false;
    }

    bool ScrollTablePage::_mode_text(char c) {
        if (c != ' ' || _mode_count() <= 1) return false;
        _cycle_mode(+1);
        return true;
    }

    void ScrollTablePage::_draw_line(uint16_t row, const char* line, uint16_t n) {
        _terminal.move_to({row, 0});
        _terminal.set_inverted(false);
        _terminal.write_chars(line, n);
        _terminal.clear_to_eol();
    }

    uint16_t ScrollTablePage::_cell_col(uint8_t i) const {
        Col cols[MAX_COLS];
        uint8_t n = _columns(cols);
        uint16_t c = (uint16_t)(MARGIN + 2);          // margin + "| "
        for (uint8_t j = 0; j < i && j < n; j++) c = (uint16_t)(c + _cell_pitch(cols[j].width));
        return c;
    }

    uint16_t ScrollTablePage::_cell_end(uint8_t i) const {
        Col cols[MAX_COLS];
        uint8_t n = _columns(cols);
        return (uint16_t)(_cell_col(i) + (i < n ? cols[i].width : 0));
    }

    uint16_t ScrollTablePage::_build_row(char* out, size_t cap, const char* const* cells) const {
        Col cols[MAX_COLS];
        uint8_t n = _columns(cols);
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
        uint8_t n = _columns(cols);
        const char* heads[MAX_COLS];
        for (uint8_t i = 0; i < n; i++) heads[i] = cols[i].head;
        char line[Config::TABLE_LINE_LEN];
        _draw_line(row, line, _build_row(line, sizeof(line), heads));
    }

    void ScrollTablePage::_draw_rule(uint16_t row, char cap, char join) {
        Col cols[MAX_COLS];
        uint8_t n = _columns(cols);
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

    uint16_t ScrollTablePage::_flex_width(uint16_t used, uint16_t cap) const {
        uint16_t cols = _terminal.cols();
        uint16_t w = (cols > used) ? (uint16_t)(cols - used) : 8;
        if (w < 8)   w = 8;
        if (w > cap) w = cap;
        return w;
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
