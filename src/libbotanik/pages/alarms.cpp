/**
 * @file alarms.cpp
 * @brief AlarmsPage implementation - the condition table and its detail panel.
 */
#include "libbotanik/pages/alarms.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_ALARMS_PAGE

#include <cstdio>

#include "libbotanik/hal/platform.h"

namespace Botanik { namespace Pages {

    const char* AlarmsPage::_mode_label(uint8_t i) const {
        static const char* const labels[MODE_COUNT] =
            {"recent", "all", "active", "unmasked"};
        return i < MODE_COUNT ? labels[i] : "all";
    }

    uint8_t AlarmsPage::_columns(Col* out) const {
        // No DESCRIPTION column: the name IS the variable, and the sentence
        // explaining it goes in the panel, where the whole of it fits.
        out[C_CODE]  = Col{ "CODE",       CODE_W  };
        out[C_NAME]  = Col{ "VARIABLE",   0       };   // flex: the base fits it
        out[C_STATE] = Col{ "ON",         STATE_W };
        out[C_OVR]   = Col{ "OVR",        OVR_W };
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
        snprintf(detail, sizeof(detail), "%u active  ENTER acks/unacks", (unsigned)active);
        _draw_legend(row, detail);
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
        // Description leads: the row's name says what it IS, not what it means.
        _panel_field((uint16_t)(br + 2), "Description",  c ? c->desc()  : nullptr);
        _panel_field((uint16_t)(br + 3), "Remediation",  c ? c->notes() : nullptr);
        _panel_field((uint16_t)(br + 4), "Acknowledged", ack);
    }

    bool AlarmsPage::_is_recent(const Alarms::AlarmCondition& c, uint64_t now) const {
        if (c.edge_ms == 0) return false;     // never fired -> no event
        if (c.active)       return true;      // on -> always recent
        if (c.recent_s() < 0) return true;      // never expires
        uint64_t window_ms = (uint64_t)(c.recent_s() == 0 ? Config::ALARM_RECENT_S
                                                       : c.recent_s()) * 1000u;
        return (now - c.edge_ms) <= window_ms;
    }

    void AlarmsPage::_collect() {
        _nvis = 0;
        if (_model == nullptr) return;
        uint64_t now = HAL::Clock::uptime_ms();
        for (uint8_t i = 0;
             i < _model->count() && _nvis < Config::ALARM_SLOTS; i++) {
            const Alarms::AlarmCondition& c = _model->condition(i);
            if (c.name() == nullptr) continue;             // undefined slot
            if (!_filter_match(c.name())) continue;
            bool include;
            switch (_mode) {
                case MODE_RECENT:   include = _is_recent(c, now);     break;
                case MODE_ACTIVE:   include = c.active;               break;
                // UNMASKED mirrors the alarm bar: active and un-ack'd. The
                // other views keep an ack'd alarm, demoted by _before().
                case MODE_UNMASKED:
                    include = c.active && !c.user_ack; break;
                default:            include = true;                   break;
            }
            if (include) _vis[_nvis++] = i;
        }
        // ALL keeps table order, so rows never reorder as alarms come and go -
        // only the ON badge changes in place.
        if (_mode == MODE_ALL) { _clamp_selection(); return; }
        // Insertion sort by _before(): _nvis is tiny, and this is stable in
        // table order on ties.
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

    void AlarmsPage::_write_name_cell(const char* line, bool selected,
                                      uint16_t l, uint16_t r) {
        _terminal.write_chars(line, l);            // margin + CODE cell + "| "
        if (selected) _terminal.set_inverted(true);
        _terminal.write_chars(line + l, (uint16_t)(r - l));
        if (selected) _terminal.set_inverted(false);
    }

    void AlarmsPage::_draw_full_row(uint16_t row, uint8_t idx,
                                    const Alarms::AlarmCondition& c, bool selected) {
        // 15 for "YY/MM/DD HH:MM" + NUL, headroom for an oddly decoded field:
        // the cell clips to EVENT_W either way, but the snprintf must not
        // truncate.
        char ev[24];
        _fmt_event(ev, sizeof(ev), c);
        const char* state = c.active ? "ON" : (c.edge_ms != 0 ? "OFF" : "");
        // The full variable name, prefix and all: a trimmed form would not
        // resolve for `get` / `override`. CODE is where the short label lives.
        // AlarmController reads its conditions through raw(), which an override
        // shadows, so the ON badge beside this IS the forced value.
        const char* forced = "";
        if (_model != nullptr) {
            bool held = false;
            if (_model->forced(idx, &held)) forced = held ? "ON" : "OFF";
        }
        const char* cells[5] = { c.code() ? c.code() : "?",
                                 c.name() ? c.name() : "?", state, forced, ev };
        char     line[Config::TABLE_LINE_LEN];
        uint16_t n = _build_row(line, sizeof(line), cells);
        // One fit for both cells the row needs, rather than one per end.
        uint16_t name_l = 0, name_end = 0, badge = 0, state_end = 0;
        _cell_span(C_NAME,  name_l, name_end);
        _cell_span(C_STATE, badge,  state_end);
        const uint16_t name_r = (uint16_t)(name_end - 1);
        _terminal.move_to({row, 0});
        _terminal.set_inverted(false);
        _write_name_cell(line, selected, name_l, name_r);
        if (c.active && !c.user_ack && n > (uint16_t)(badge + 2)) {
            // "ON" leads the STATE cell: split so only it inverts.
            _terminal.write_chars(line + name_r, (uint16_t)(badge - name_r));
            _terminal.set_inverted(true);
            _terminal.write_chars(line + badge, 2);
            _terminal.set_inverted(false);
            _terminal.write_chars(line + badge + 2, (uint16_t)(n - badge - 2));
        } else {
            _terminal.write_chars(line + name_r, (uint16_t)(n - name_r));
        }
        _terminal.clear_to_eol();
    }

    void AlarmsPage::_draw_data_row(uint16_t row, uint16_t vi) {
        if (vi >= _nvis) { _blank_line(row); return; }    // past the set: blank it
        const Alarms::AlarmCondition& c = _model->condition(_vis[vi]);
        _draw_full_row(row, _vis[vi], c, vi == _selected);
    }

    const char* AlarmsPage::_selected_cell() const {
        if (_model == nullptr || _selected >= _nvis) return nullptr;
        // The variable name, matching the cell the cursor sits on: `get` and
        // `override` resolve it, so a yank into the shell prompt is usable.
        return _model->condition(_vis[_selected]).name();
    }

    void AlarmsPage::_toggle_ack() {
        if (_model == nullptr || _selected >= _nvis) return;
        const Alarms::AlarmCondition& c = _model->condition(_vis[_selected]);
        if (c.name() == nullptr) return;
        if (c.user_ack) _model->unack(c.name());
        else            _model->ack(c.name());
        _dirty = true;   // either way the badge and the sort move
    }

    void AlarmsPage::_draw_structure() {
        uint16_t legend = _legend_row();
        _blank_line(1);                                     // top margin
        _draw_header(HEADER_ROW);
        _blank_line((uint16_t)(legend - 1));                // blank above legend
        _blank_line((uint16_t)(legend + 1));                // blank below legend
    }

    void AlarmsPage::handle_text(char c) {
        // Typing narrows the table (base). The legend row outranks the table
        // rows (focus()), so the badge reaches the wire ahead of the repaint.
        (void)_filter_text(c);
    }

    void AlarmsPage::handle_command(Command cmd, uint16_t) {
        // ENTER toggles the ack, not 'a': every table takes typing as its filter, so the
        // one key that acts on a row has to be one that cannot be typed.
        if (cmd == Command::ENTER) { _toggle_ack(); return; }
        if (_filter_command(cmd)) return;            // BACKSPACE pops, ESC clears
        if (_mode_command(cmd)) return;              // LEFT/RIGHT change the view
        if (_nav_command(cmd)) { _dirty = true; return; }
    }

    void AlarmsPage::focus() {
        // The legend badges acknowledge a keypress, so their row must beat the
        // table rows. Must be declared here: the Console hands the content band
        // back as plain structure on every page switch.
        _claim_legend_row(_legend_row());
        _dirty = true;   // draw the whole page fresh (the terminal diff pares it)
    }

    void AlarmsPage::tick(bool active) {
        if (!active) return;
        // Rebuild on the value cadence, or immediately after a nav. Deciding
        // what to repaint is the terminal back-buffer's job (per cell); this is
        // only the guard against re-collecting twenty rows every beat.
        uint64_t now = HAL::Clock::uptime_ms();
        if (!_dirty && now - _last_refresh_ms < Config::PAGE_HOT_MS) return;
        _last_refresh_ms = now;
        _dirty = false;
        uint16_t window = _window();
        if (window == 0) return;
        // Rebuild the view set, re-clamp the window, redraw the whole table
        // immediate-mode: the terminal's diff pares it (@ref dev_screen).
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

#endif // BOTANIK_FEATURE_ALARMS_PAGE
