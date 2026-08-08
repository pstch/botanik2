/**
 * @file alarms.cpp
 * @brief AlarmsPage implementation - the condition table and its detail panel.
 */
#include "libbotanik/pages/alarms.h"

#include <cstdio>                           // snprintf

#include "libbotanik/hal/platform.h"        // HAL::Clock::uptime_ms

namespace Botanik { namespace Pages {

    const char* AlarmsPage::_mode_label(uint8_t i) const {
        static const char* const labels[MODE_COUNT] =
            {"recent", "all", "active", "unmasked"};
        return i < MODE_COUNT ? labels[i] : "all";
    }

    uint8_t AlarmsPage::_columns(Col* out) const {
        // No DESCRIPTION column: the name IS the variable, so the row says
        // what it is, and the sentence explaining it belongs in the panel
        // where there is room for the whole of it.
        out[C_CODE]  = Col{ "CODE",       CODE_W  };
        out[C_NAME]  = Col{ "VARIABLE",   _name_w() };
        out[C_STATE] = Col{ "ON",         STATE_W };
        out[C_FORCED]= Col{ "FRC",        FORCED_W };
        out[C_EVENT] = Col{ "LAST EVENT", EVENT_W };
        return 5;
    }

    void AlarmsPage::_fmt_event(char* buf, size_t cap, const Alarms::AlarmCondition& c) {
        if (!c.active && c.edge_ms == 0) { if (cap) buf[0] = '\0'; return; }
        Utils::CalendarTime t = Utils::cal_time_ms(c.edge_ms);
        snprintf(buf, cap, "%02u/%02u/%02u %02u:%02u",
                 (unsigned)(t.year % 100), (unsigned)t.month, (unsigned)t.day,
                 (unsigned)t.hour, (unsigned)t.minute);
    }

    void AlarmsPage::_draw_footer(uint16_t row, uint8_t active) {
        char detail[32];
        snprintf(detail, sizeof(detail), "%u active   a:ack", (unsigned)active);
        // No group: the alarms table's views ARE its modes, so the badge and
        // its neighbours already say everything a group field would.
        _draw_mode_line(row, nullptr, detail);
    }

    void AlarmsPage::_draw_notes() {
        const Alarms::AlarmCondition* c =
            (_model != nullptr && _selected < _nvis)
                ? &_model->condition(_vis[_selected]) : nullptr;
        char ackbuf[24];
        const char* ack = "-";
        if (c != nullptr && c->user_ack) {
            Utils::CalendarTime t = Utils::cal_time_ms(c->ack_ms);
            snprintf(ackbuf, sizeof(ackbuf), "%02u/%02u/%02u %02u:%02u",
                     (unsigned)(t.year % 100), (unsigned)t.month, (unsigned)t.day,
                     (unsigned)t.hour, (unsigned)t.minute);
            ack = ackbuf;
        }
        uint16_t br = _brule_row();
        _blank_line((uint16_t)(br + 1));
        // Description leads: it was a table column until the name became the
        // variable's own, which says what the row IS but not what it means.
        _panel_field((uint16_t)(br + 2), "Description",  c ? c->desc  : nullptr);
        _panel_field((uint16_t)(br + 3), "Remediation",  c ? c->notes : nullptr);
        _panel_field((uint16_t)(br + 4), "Acknowledged", ack);
    }

    bool AlarmsPage::_is_recent(const Alarms::AlarmCondition& c, uint64_t now) const {
        if (c.edge_ms == 0) return false;     // never fired -> no event
        if (c.active)       return true;      // on -> always recent
        if (c.recent_s < 0) return true;      // never expires
        uint64_t window_ms = (uint64_t)(c.recent_s == 0 ? Config::ALARM_RECENT_S
                                                       : c.recent_s) * 1000u;
        return (now - c.edge_ms) <= window_ms;
    }

    void AlarmsPage::_collect() {
        _nvis = 0;
        if (_model == nullptr) return;
        uint64_t now = HAL::Clock::uptime_ms();
        for (uint8_t i = 0;
             i < _model->count() && _nvis < Config::ALARM_SLOTS; i++) {
            const Alarms::AlarmCondition& c = _model->condition(i);
            if (c.name == nullptr) continue;             // undefined slot
            bool include;
            switch (_mode) {
                case MODE_RECENT:   include = _is_recent(c, now);     break;
                case MODE_ACTIVE:   include = c.active;               break;
                // UNMASKED mirrors the alarm bar, so an ack'd alarm drops
                // out of it too (the ALL / ACTIVE / RECENT views keep it,
                // demoted by _before()). With masking removed this is the
                // active-and-un-ack'd set.
                case MODE_UNMASKED:
                    include = c.active && !c.user_ack; break;
                default:            include = true;                   break;
            }
            if (include) _vis[_nvis++] = i;
        }
        // ALL mode lists every alarm in definition (table) order, so the
        // rows never reorder as alarms come and go - only the ON badge
        // changes in place. The other modes order active-first (_before()).
        if (_mode == MODE_ALL) { _clamp_selection(); return; }
        // Insertion sort by _before(): runs once per (gated) repaint,
        // _nvis is nearly always tiny, so it beats a general sort and is
        // stable in table order on ties.
        for (uint8_t a = 1; a < _nvis; a++) {
            uint8_t v = _vis[a];
            const Alarms::AlarmCondition& cv = _model->condition(v);
            int b = (int)a - 1;
            while (b >= 0 && _before(cv, _model->condition(_vis[b]))) {
                _vis[b + 1] = _vis[b];
                b--;
            }
            _vis[b + 1] = v;
        }
        _clamp_selection();
    }

    void AlarmsPage::_draw_rules(uint16_t window) {
        // The alarms rules carry the arrow on EVERY column mark, not just
        // the outer caps - hence the join argument.
        char top = _top ? '^' : '+';
        char bot = _hidden_below(window) ? 'v' : '+';
        _draw_rule(RULE_ROW,     top, top);
        _draw_rule(_brule_row(), bot, bot);
    }

    void AlarmsPage::_write_code_cell(const char* line, bool selected) {
        const uint16_t l = _cell_col(C_CODE), r = _code_cell_r();
        _terminal.write_chars(line, l);                        // margin + "| "
        if (selected) _terminal.set_inverted(true);
        _terminal.write_chars(line + l, (uint16_t)(r - l));
        if (selected) _terminal.set_inverted(false);
    }

    void AlarmsPage::_draw_full_row(uint16_t row, uint8_t idx,
                                    const Alarms::AlarmCondition& c, bool selected) {
        // 15 for "YY/MM/DD HH:MM" + NUL, rounded up to what a field that
        // decoded oddly would need: the cell clips to EVENT_W either way,
        // and a truncating snprintf is not something to leave to chance.
        char ev[24];
        _fmt_event(ev, sizeof(ev), c);
        const char* state = c.active ? "ON" : (c.edge_ms != 0 ? "OFF" : "");
        // The full variable name, prefix and all: it is what `get` and
        // `override` want typed at them, so showing a trimmed form here
        // would be handing out a name that does not resolve. The CODE
        // column is where the short label lives.
        // An alarm is a SCOPE_A row, so it can be forced like any other - and
        // AlarmController reads its conditions through raw(), which an override
        // shadows. So the ON badge beside this IS the forced value, and without
        // this column a hand-set alarm looks exactly like a real one.
        const char* forced = "";
        if (_model != nullptr) {
            bool held = false;
            if (_model->forced(idx, &held)) forced = held ? "SET" : "CLR";
        }
        const char* cells[5] = { c.code ? c.code : "?",
                                 c.name ? c.name : "?", state, forced, ev };
        char     line[Config::TABLE_LINE_LEN];
        uint16_t n = _build_row(line, sizeof(line), cells);
        const uint16_t code_r = _code_cell_r(), badge = _cell_col(C_STATE);
        _terminal.move_to({row, 0});
        _terminal.set_inverted(false);
        _write_code_cell(line, selected);
        if (c.active && !c.user_ack && n > (uint16_t)(badge + 2)) {
            // "ON" leads the STATE cell: split so only it inverts.
            _terminal.write_chars(line + code_r, (uint16_t)(badge - code_r));
            _terminal.set_inverted(true);
            _terminal.write_chars(line + badge, 2);
            _terminal.set_inverted(false);
            _terminal.write_chars(line + badge + 2, (uint16_t)(n - badge - 2));
        } else {
            _terminal.write_chars(line + code_r, (uint16_t)(n - code_r));
        }
        _terminal.clear_to_eol();
    }

    void AlarmsPage::_draw_data_row(uint16_t row, uint16_t vi) {
        if (vi >= _nvis) { _blank_line(row); return; }    // past the set: blank it
        const Alarms::AlarmCondition& c = _model->condition(_vis[vi]);
        _draw_full_row(row, _vis[vi], c, vi == _selected);
    }

    void AlarmsPage::_ack_selected() {
        if (_model == nullptr || _selected >= _nvis) return;
        const Alarms::AlarmCondition& c = _model->condition(_vis[_selected]);
        if (c.name != nullptr) _model->ack(c.name);
        _dirty = true;   // ack changes the badge / sort
    }

    void AlarmsPage::_draw_structure() {
        uint16_t legend = _legend_row();
        _blank_line(1);                                     // top margin
        _draw_header(HEADER_ROW);
        _blank_line((uint16_t)(legend - 1));                // blank above legend
        _blank_line((uint16_t)(legend + 1));                // blank below legend
    }

    void AlarmsPage::handle_text(char c) {
        // SPACE cycles the filter (base); the legend row already outranks the
        // table rows (declared in focus()), so the badge reaches the wire
        // ahead of the repaint the same keypress triggered.
        if (_mode_text(c)) return;
        if (c == 'a' || c == 'A') _ack_selected();   // ack the selected condition
    }

    void AlarmsPage::handle_command(Command cmd, uint16_t) {
        if (_mode_command(cmd)) return;              // LEFT/RIGHT change filter
        if (_nav_command(cmd)) { _dirty = true; return; }
        if (cmd == Command::ENTER) { _top = 0; _selected = 0; _dirty = true; }  // jump to top
    }

    void AlarmsPage::focus() {
        // The legend carries the mode badge, which acknowledges a keypress and
        // so must beat the table rows it sits under. Declared here because the
        // Console hands the content band back on every page switch.
        _claim_mode_row(_legend_row());
        _dirty = true;   // draw the whole page fresh (the terminal diff pares it)
    }

    void AlarmsPage::tick(bool active) {
        if (!active) return;
        // Rebuild on the value cadence (or immediately after a nav), exactly
        // as the MONITOR and PARAMS tables do. This page used to carry three
        // 64-bit signatures of the alarm set to decide whether a repaint was
        // needed at all; that is the terminal back-buffer's job, and it does
        // it per cell. What is left is the cheap guard the other tables use:
        // don't re-collect and re-format twenty rows every single beat.
        uint64_t now = HAL::Clock::uptime_ms();
        if (!_dirty && now - _last_refresh_ms < Config::PAGE_HOT_MS) return;
        _last_refresh_ms = now;
        _dirty = false;
        uint16_t window = _window();
        if (window == 0) return;
        // Rebuild the view set and re-clamp the window, then redraw the whole
        // table immediate-mode: the terminal's back-buffer diff emits only the
        // cells that actually changed, so the page keeps no paint shadow.
        _collect();
        uint16_t maxtop = _max_top();
        if (_top > maxtop) _top = maxtop;
        _draw_structure();
        _draw_rules(window);
        _draw_notes();
        _draw_footer(_legend_row(), _model ? _model->active_count() : 0);
        for (uint16_t vi = 0; vi < window; vi++)
            _draw_data_row((uint16_t)(DATA_TOP + vi), (uint16_t)(_top + vi));
    }
}}
