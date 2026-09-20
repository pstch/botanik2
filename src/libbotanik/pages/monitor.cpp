/**
 * @file monitor.cpp
 * @brief MonitorPage implementation - the published-variable table.
 */
#include "libbotanik/pages/monitor.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_MONITOR_PAGE

#include <cstdio>
#include <cstring>

#include "libbotanik/hal/platform.h"
#include "libbotanik/utils/text.h"

namespace Botanik { namespace Pages {

    void MonitorPage::focus() {
        _field.end();
        // The footer carries the filter badge, which acknowledges a keypress,
        // so it must beat the table rows it sits under.
        _claim_legend_row(_footer_row());
        _clamp_selection();
        _follow_selection();
        _dirty = true;
    }

    void MonitorPage::handle_command(Command cmd, uint16_t count) {
        if (_field.open()) { _handle_edit_command(cmd, count); return; }
        // The override keys first: ^O opens the field, and the filter would
        // otherwise never see them anyway.
        if (cmd == Command::CTRL_O) { _start_edit();      return; }
        if (cmd == Command::CTRL_V) { _toggle_validity(); return; }
        if (cmd == Command::CTRL_R) { _clear_override();  return; }
        if (cmd == Command::CTRL_S) { _save_override();   return; }
        if (_filter_command(cmd)) return;       // BACKSPACE pops, ESC clears
        if (_mode_command(cmd)) return;         // LEFT/RIGHT change the view
        if (_nav_command(cmd)) _dirty = true;   // redraw next tick, not the cadence
    }

    void MonitorPage::handle_text(char c) {
        if (!_field.open()) { (void)_filter_text(c); return; }   // narrows the table
        if (c < 0x20 || c == 0x7F) return;                       // printable only
        _field.handle_text(c);
    }

    void MonitorPage::tick(bool active) {
        if (!active) return;
        // While the field is open it owns the bottom row and is redrawn per
        // keystroke; leave the table and panel as they were drawn.
        if (_field.open()) return;
        // Rebuild on the value cadence, or immediately after a nav. The
        // terminal's diff decides what reaches the wire; this only stops the
        // page re-formatting every row every beat for nothing.
        uint64_t now = HAL::Clock::uptime_ms();
        if (!_dirty && now - _last_refresh_ms < Config::PAGE_HOT_MS) return;
        _last_refresh_ms = now;
        _dirty = false;
        // A live view selects on readings, so a row can join or leave the set
        // between two refreshes with nothing to bump a generation.
        if (_mode_is_live()) _invalidate_rows();
        uint16_t win = _window();
        _clamp_selection();
        _follow_selection();
        _draw_structure();
        _draw_rules();
        // The band must go out BEFORE the panel and footer: rows first seed
        // _ref_at()'s memo at _top and walk forward, and _selected is inside
        // the band, so the two trailing lookups resume instead of rescanning.
        // One walk per refresh, not three.
        for (uint16_t vi = 0; vi < win; vi++) {
            const uint16_t display_idx = (uint16_t)(_top + vi);
            _draw_row(display_idx, (uint16_t)(DATA_TOP + vi),
                      display_idx == _selected);
        }
        _draw_panel();
        _draw_footer();
        _draw_prompt_row();
    }

    const char* MonitorPage::_mode_label(uint8_t i) const {
        static const char* const labels[MODE_COUNT] = {"all", "invalid", "override"};
        return i < MODE_COUNT ? labels[i] : "all";
    }

    bool MonitorPage::_visible(uint16_t i) const {
        Vars::Var h = _io->var(i);
        if (!Vars::published(h.meta())) return false;
        if (!_filter_match(h.meta().name)) return false;
        switch (_mode) {
            case MODE_INVALID:  return !h.valid();
            case MODE_OVERRIDE: return h.override_on();
            default:            return true;
        }
    }

    uint16_t MonitorPage::_total() const {
        if (_io == nullptr) return 0;
        if (_total_gen == _filter_gen) return _total_n;
        uint16_t t = 0;
        for (uint16_t i = 0; i < _io->count(); i++) if (_visible(i)) t++;
        _total_n = t;
        _total_gen = _filter_gen;
        return t;
    }

    uint8_t MonitorPage::_columns(Col* out) const {
        out[C_KIND]  = Col{ "KIND",     KIND_W     };
        out[C_NAME]  = Col{ "VARIABLE", NAME_W     };
        out[C_VALUE] = Col{ "VALUE",    0          };   // flex: the base fits it
        out[C_UNIT]  = Col{ "UNIT",     UNIT_W     };
        // A forced reading is indistinguishable from a real one in the VALUE
        // column - that is the whole hazard of an override - so the table
        // says so per row rather than only in the detail panel.
        out[C_OVR]   = Col{ "OVERRIDE", OVR_W      };
        return 5;
    }

    uint16_t MonitorPage::_ref_at(uint16_t display_idx, bool& ok) const {
        if (_io != nullptr) {
            // Resume from the last answer when the caller is walking
            // forward, which is what drawing a window does. Only ever
            // forward: a memo for a LATER row says nothing about an earlier
            // one without re-counting from the top.
            uint16_t at = 0, var_idx = 0;
            if (_memo_ok && display_idx >= _memo_display) {
                at = _memo_display; var_idx = _memo_var;
            }
            for (; var_idx < _io->count(); var_idx++) {
                if (!_visible(var_idx)) continue;
                if (at == display_idx) {
                    ok = true;
                    _memo_display = display_idx;
                    _memo_var     = var_idx;
                    _memo_ok      = true;
                    return var_idx;
                }
                at++;
            }
        }
        ok = false;
        return Vars::VAR_NONE;
    }

    uint16_t MonitorPage::_row_text(const Fit& fit, uint16_t display_idx,
                                   char* out, size_t cap) const {
        bool ok = false;
        const uint16_t ref = _ref_at(display_idx, ok);
        if (!ok) { if (cap) out[0] = 0; return 0; }
        char val[VAL_CAP]; val[0] = 0;
        char ovr[VAL_CAP]; ovr[0] = '-'; ovr[1] = 0;
        const char* nm = "", *un = "", *sc = "";
        if (_io != nullptr) {
            Vars::Var h = _io->var(ref);
            const Vars::Descriptor& m = h.meta();
            if (m.name != nullptr) nm = m.name;
            if (m.unit != nullptr) un = m.unit;
            // The KIND, not the group: the name already carries the group, in
            // the column the filter matches against.
            sc = Vars::kind_label(m.kind);
            // The LIVE value, not the forced one - the OVERRIDE column shows
            // the force. Printing it in both hides what the operator is
            // deciding on: whether the reading underneath has come back.
            _adopt(val, sizeof(val), _live_text(h, val, sizeof(val)));
            _flag_value(h, val, sizeof(val));
            if (h.override_on()) {
                _adopt(ovr, sizeof(ovr), Vars::var_format(h, ovr, sizeof(ovr)));
                // A force can itself be "read as a dead sensor"; say so, or the
                // column claims a number the row is not actually reporting.
                if (!h.valid()) _prefix(ovr, sizeof(ovr), "[I]");
            }
        }
        const char* cells[5] = { sc, nm, val, un, ovr };
        return _build_row(fit, out, cap, cells);
    }

    uint16_t MonitorPage::_selected_ref() const {
        // A live view (`invalid`, `override`) selects on READINGS, so the set
        // can change with no keypress: a variable going good between the paint
        // and the keystroke shifts every row under the cursor, and a fresh walk
        // would then name a different variable from the one on screen. So the
        // drawn row stands - unless the cursor has MOVED since, which is the
        // operator changing their mind and has to re-resolve.
        if (_pin_state.ok && _pin_state.display_idx == _selected) return _pin_state.var_idx;
        bool ok = false;
        const uint16_t var_idx = _ref_at(_selected, ok);
        _pin(_selected, var_idx, ok);
        return ok ? var_idx : Vars::VAR_NONE;
    }

    void MonitorPage::_draw_row(uint16_t display_idx, uint16_t screen_row, bool selected) {
        char line[LINE_CAP];
        if (selected) {
            // Pinned as the line goes out: the one moment the screen and the
            // index are known to agree.
            bool ok = false;
            const uint16_t var_idx = _ref_at(display_idx, ok);
            _pin(display_idx, var_idx, ok);
        }
        const Fit fit = _fit_columns();          // one solve, both ends
        const uint16_t n = _row_text(fit, display_idx, line, sizeof(line));
        uint16_t name_l = 0, name_r = 0;
        _cell_span(fit, C_NAME, name_l, name_r);
        _draw_selected_line(screen_row, line, n, name_l, name_r, selected);
    }

    void MonitorPage::_prefix(char* buf, size_t cap, const char* tag) {
        char tmp[VAL_CAP];
        snprintf(tmp, sizeof(tmp), "%s%s", tag, buf);
        snprintf(buf, cap, "%s", tmp);
    }

    const char* MonitorPage::_live_text(Vars::Var h, char* buf, size_t cap) const {
        return Vars::var_format(h, h.live(), buf, cap);
    }

    void MonitorPage::_flag_value(Vars::Var h, char* buf, size_t cap) {
        // [O] forced, [I] the reading underneath is not good, [OI] both: two
        // independent facts, so two independent letters.
        const bool o = h.override_on();
        const bool i = !h.valid();
        if (!o && !i) return;
        char tag[6] = "[";
        size_t n = 1;
        if (o) tag[n++] = 'O';
        if (i) tag[n++] = 'I';
        tag[n++] = ']';
        tag[n] = '\0';
        _prefix(buf, cap, tag);
    }

    void MonitorPage::_describe_binding(const Vars::Descriptor& d, char* out,
                                        size_t cap) const {
        // The unit number is what the wire uses; the TAG is what the plant
        // calls the box, so an operator need not look "dev 34" up in a
        // spreadsheet.
        Vars::binding_phrase(d, out, cap,
                             (_device_name != nullptr)
                                 ? _device_name(_device_ctx, d.bind.device)
                                 : nullptr);
    }

    void MonitorPage::_draw_rules() {
        uint16_t win = _window(), total = _total();
        _draw_rule(TRULE_ROW, _top > 0 ? '^' : '+');
        _draw_rule(_brule_row(), (uint16_t)(_top + win) < total ? 'v' : '+');
    }

    const char* MonitorPage::_selected_cell() const {
        const uint16_t ref = _selected_ref();
        if (ref == Vars::VAR_NONE || _io == nullptr) return nullptr;
        return _io->var(ref).meta().name;
    }

    void MonitorPage::_draw_panel() {
        uint16_t br = _brule_row();
        _blank_line((uint16_t)(br + 1));    // breathe under the table
        // The same row a key would act on, so the panel cannot describe one
        // variable while ^R releases another.
        const uint16_t ref = _selected_ref();
        const bool have = (ref != Vars::VAR_NONE) && _io != nullptr;
        Vars::Var h = have ? _io->var(ref) : Vars::Var();
        _panel_field((uint16_t)(br + 2), "Description", have ? h.description() : nullptr);
        _panel_field((uint16_t)(br + 3), "Notes",       have ? h.notes() : nullptr);
        // "Mapping", not "Source": for an OUTPUT nothing comes FROM the row,
        // it goes there. The field names which point on which device the row
        // is mapped to, in either direction.
        char bind[72];
        if (have) _describe_binding(h.meta(), bind, sizeof(bind));
        _panel_field((uint16_t)(br + 4), "Mapping", have ? bind : nullptr);

        // Quality, plus whichever of the two things qualifies it: for a
        // READING its age (GOOD from four minutes ago is not GOOD from this
        // beat), for a DRIVEN or COMPUTED row the program that wrote it.
        char qual[80];
        if (have) {
            const char* q_label = Vars::quality_label((uint64_t)h.quality());
            size_t n = (size_t)snprintf(qual, sizeof(qual), "%s",
                                        (q_label != nullptr) ? q_label : "?");
            // A quality is a row of its own, so it can be forced like any
            // other. Say when the reading on show is an operator's rather than
            // the driver's.
            if (h.quality_var().override_on() && n < sizeof(qual))
                n += (size_t)snprintf(qual + n, sizeof(qual) - n, ", forced");

            if (h.kind() == Vars::K_INPUT && h.written() && n < sizeof(qual)) {
                char age[16];
                const uint32_t secs = h.age_ms(HAL::Clock::uptime_ms()) / 1000u;
                // format_age already says "ago"; the width is fixed so the
                // field does not reflow as the reading gets older.
                n += (size_t)snprintf(qual + n, sizeof(qual) - n, ", %s",
                                      Utils::format_age(age, sizeof(age), secs));
            }
        }
        _panel_field((uint16_t)(br + 5), "Quality", have ? qual : nullptr);
        _blank_line((uint16_t)(br + 6));   // blank: the OVERRIDE column says it
    }

    void MonitorPage::_draw_footer() {
        // What is owed to flash, on the page that takes overrides: an override
        // is live the moment it commits and gone at the next boot until ^S.
        char detail[24];
        detail[0] = '\0';
        const uint16_t unsaved = (_io != nullptr) ? _io->unsaved_override_count() : 0;
        if (unsaved != 0)
            snprintf(detail, sizeof(detail), "%u unsaved  ^S save", (unsigned)unsaved);
        _draw_legend(_footer_row(), detail[0] != '\0' ? detail : nullptr);
    }

    // Overrides


    uint16_t MonitorPage::_edit_label(char* buf, size_t cap) const {
        const char* nm = _io->var(_edit_ref).meta().name;
        int n = snprintf(buf, cap, " %s = ", nm != nullptr ? nm : "?");
        return (n < 0) ? 0 : (uint16_t)n;
    }

    void MonitorPage::_draw_prompt_row() {
        const uint16_t row = _prompt_row();
        if (_field.open()) {
            char label[40];
            const uint16_t lw = _edit_label(label, sizeof(label));
            _field.redraw(row, label, lw);
            return;
        }
        _terminal.set_cursor_visible(false);
        const char* say = (HAL::Clock::uptime_ms() < _said_until_ms) ? _said : "";
        _draw_line(row, say, (uint16_t)std::strlen(say));
    }

    void MonitorPage::_start_edit() {
        if (_io == nullptr) return;
        const uint16_t ref = _selected_ref();
        if (ref == Vars::VAR_NONE) return;
        Vars::Var h = _io->var(ref);
        // A T_STR's value lives in the text pool, which an override cannot
        // hold: say so rather than opening a field whose every commit fails.
        if (h.meta().type == Vars::T_STR) { _say("a text row cannot be forced"); return; }
        _edit_ref = ref;

        char label[40];
        const uint16_t lw = _edit_label(label, sizeof(label));
        // Seeded with what the row reads NOW - a force usually starts from the
        // reading it is replacing.
        char val[OVR_INPUT_LEN];
        const char* v = Vars::var_format(h, val, sizeof(val));
        const uint16_t n = (uint16_t)std::strlen(v);
        if (v != val) { std::strncpy(val, v, sizeof(val) - 1); val[sizeof(val) - 1] = 0; }

        _field.begin(_prompt_row(), label, lw, val, n);
    }

    void MonitorPage::_cancel_edit() {
        _field.end();
        _dirty = true;
        _draw_prompt_row();              // hide the cursor, clear the field
    }

    void MonitorPage::_commit_edit() {
        char line[OVR_INPUT_LEN];
        _field.read(line, sizeof(line));
        if (_io->var(_edit_ref).override_set(line) != Vars::SET_OK) {
            _field.refuse();             // stay in the field, flag it
            return;
        }
        _field.end();
        _dirty = true;                   // the OVERRIDE column says the rest
        _say("override set - ^S saves this row");
        _draw_prompt_row();
    }

    void MonitorPage::_handle_edit_command(Command cmd, uint16_t count) {
        switch (_field.handle_command(cmd, count)) {
            case EditOutcome::COMMIT: _commit_edit(); break;
            case EditOutcome::CANCEL: _cancel_edit(); break;
            case EditOutcome::HANDLED: break;
        }
    }

    void MonitorPage::_toggle_validity() {
        if (_io == nullptr) return;
        const uint16_t ref = _selected_ref();
        if (ref == Vars::VAR_NONE) return;
        // Making a row read like a dead sensor is forcing its `_qlt` companion:
        // an ordinary override of an ordinary row, saved and released with the
        // same two keys. A row with no companion has no quality to force.
        const Vars::Var q = _io->var(ref).quality_var();
        if (q.null()) { _say("this row carries no quality"); return; }
        const bool was_valid = _io->var(ref).valid();
        q.override_set(Vars::Scalar::U(was_valid ? Vars::Q_NONE : Vars::Q_GOOD));
        _say(was_valid ? "forced to read invalid" : "forced to read valid");
        _dirty = true;
    }

    void MonitorPage::_clear_override() {
        if (_io == nullptr) return;
        const uint16_t ref = _selected_ref();
        if (ref == Vars::VAR_NONE) return;
        Vars::Var h = _io->var(ref);
        if (!h.override_on()) { _say("not overridden"); return; }
        // Through Persist: releasing forgets the stored copy too, or the
        // override would come back at the next boot.
        if (_persist != nullptr) _persist->override_clear(h);
        else                     h.override_clear();
        _say("override released");
    }

    void MonitorPage::_save_override() {
        if (_io == nullptr) return;
        const uint16_t ref = _selected_ref();
        if (ref == Vars::VAR_NONE) return;
        // The row under the cursor, not every unsaved one: what is being
        // committed to flash should be the row the operator is looking at.
        if (_persist == nullptr) { _say("nothing to save to"); return; }
        switch (_persist->save_override(_io->var(ref))) {
            case Vars::SET_OK:            _say("override saved");                 break;
            case Vars::SET_NOT_PERSISTED: _say("NOT saved - storage refused it"); break;
            default:                      _say("nothing forced on this row");     break;
        }
        _dirty = true;      // the legend's unsaved count moved
    }
}}

#endif // BOTANIK_FEATURE_MONITOR_PAGE
