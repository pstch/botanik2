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
        _editing = false;
        // The footer carries the filter badge, which acknowledges a keypress,
        // so it must beat the table rows it sits under.
        _claim_legend_row(_footer_row());
        _clamp_selection();
        _follow_selection();
        _dirty = true;
    }

    void MonitorPage::handle_command(Command cmd, uint16_t count) {
        if (_editing) { _handle_edit_command(cmd, count); return; }
        // The override keys first: ^O opens the field, and the filter would
        // otherwise never see them anyway.
        if (cmd == Command::CTRL_O) { _start_edit();      return; }
        if (cmd == Command::CTRL_V) { _toggle_validity(); return; }
        if (cmd == Command::CTRL_R) { _clear_override();  return; }
        if (cmd == Command::CTRL_S) { _save_override();   return; }
        if (_filter_command(cmd)) return;       // BACKSPACE pops, ESC clears
        if (_nav_command(cmd)) _dirty = true;   // redraw next tick, not the cadence
    }

    void MonitorPage::handle_text(char c) {
        if (!_editing) { (void)_filter_text(c); return; }   // narrows the table
        if (c < 0x20 || c == 0x7F) return;                  // printable only
        if (_edit_error) _clear_error();
        _editor.insert_characters(&c, 1);
    }

    void MonitorPage::tick(bool active) {
        if (!active) return;
        // While the field is open it owns the bottom row and is redrawn per
        // keystroke; leave the table and panel as they were drawn.
        if (_editing) return;
        // Rebuild on the value cadence, or immediately after a nav. The
        // terminal's diff decides what reaches the wire; this only stops the
        // page re-formatting every row every beat for nothing.
        uint64_t now = HAL::Clock::uptime_ms();
        if (!_dirty && now - _last_refresh_ms < Config::PAGE_HOT_MS) return;
        _last_refresh_ms = now;
        _dirty = false;
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
            uint16_t r = (uint16_t)(_top + vi);
            _draw_row(r, (uint16_t)(DATA_TOP + vi), r == _selected);
        }
        _draw_panel();
        _draw_footer();
        _draw_prompt_row();
    }

    bool MonitorPage::_visible(uint16_t i) const {
        const Vars::Descriptor& d = *_io->var(i).meta();
        if (!Vars::var_published(d)) return false;
        return _filter_match(d.name);
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
        out[C_SCOPE] = Col{ "SCOPE",    SCOPE_W    };
        out[C_NAME]  = Col{ "VARIABLE", NAME_W     };
        out[C_VALUE] = Col{ "VALUE",    0          };   // flex: the base fits it
        out[C_UNIT]  = Col{ "UNIT",     UNIT_W     };
        // A forced reading is indistinguishable from a real one in the VALUE
        // column - that is the whole hazard of an override - so the table
        // says so per row rather than only in the detail panel.
        out[C_OVR]   = Col{ "OVERRIDE", OVR_W      };
        return 5;
    }

    Vars::VarRef MonitorPage::_ref_at(uint16_t r, bool& ok) const {
        if (_io != nullptr) {
            // Resume from the last answer when the caller is walking
            // forward, which is what drawing a window does. Only ever
            // forward: a memo for a LATER row says nothing about an earlier
            // one without re-counting from the top.
            uint16_t n = 0, i = 0;
            if (_memo_ok && r >= _memo_row) { n = _memo_row; i = _memo_idx; }
            for (; i < _io->count(); i++) {
                if (!_visible(i)) continue;
                if (n == r) {
                    ok = true;
                    _memo_row = r; _memo_idx = i; _memo_ok = true;
                    Vars::VarRef v = { i };
                    return v;
                }
                n++;
            }
        }
        ok = false;
        Vars::VarRef none = { Vars::VAR_NONE };
        return none;
    }

    uint16_t MonitorPage::_row_text(uint16_t r, char* out, size_t cap) const {
        bool ok = false;
        Vars::VarRef ref = _ref_at(r, ok);
        if (!ok) { if (cap) out[0] = 0; return 0; }
        char val[VAL_CAP]; val[0] = 0;
        char ovr[VAL_CAP]; ovr[0] = '-'; ovr[1] = 0;
        const char* nm = "", *un = "", *sc = "";
        if (_io != nullptr) {
            Vars::Var h = _io->var(ref);
            const Vars::Descriptor* m = h.meta();
            if (m->name != nullptr) nm = m->name;
            if (m->unit != nullptr) un = m->unit;
            // Scope, not the group: the name already carries the group, in
            // the column the filter matches against.
            sc = Vars::var_scope(*m);
            // The LIVE value, not the forced one - the OVERRIDE column shows
            // the force. Printing it in both hides what the operator is
            // deciding on: whether the reading underneath has come back.
            _adopt(val, sizeof(val), _live_text(h, val, sizeof(val)));
            _flag_value(h, val, sizeof(val));
            if (h.override().present()) {
                _adopt(ovr, sizeof(ovr), Vars::var_format(*_io, h, ovr, sizeof(ovr)));
                // A force can itself be "read as a dead sensor"; say so, or the
                // column claims a number the row is not actually reporting.
                if (!h.valid()) _prefix(ovr, sizeof(ovr), "[I]");
            }
        }
        const char* cells[5] = { sc, nm, val, un, ovr };
        return _build_row(out, cap, cells);
    }

    void MonitorPage::_draw_row(uint16_t r, uint16_t row, bool selected) {
        char line[LINE_CAP];
        uint16_t n = _row_text(r, line, sizeof(line));
        uint16_t name_l = 0, name_r = 0;
        _cell_span(C_NAME, name_l, name_r);      // one fit, both ends
        _draw_selected_line(row, line, n, name_l, name_r,
                            selected);
    }

    void MonitorPage::_prefix(char* buf, size_t cap, const char* tag) {
        char tmp[VAL_CAP];
        snprintf(tmp, sizeof(tmp), "%s%s", tag, buf);
        snprintf(buf, cap, "%s", tmp);
    }

    const char* MonitorPage::_live_text(Vars::Var h, char* buf, size_t cap) const {
        const Vars::Descriptor* d = h.meta();
        if (d->type == Vars::T_STR) {
            const char* t = _io->text(h);
            return (t != nullptr) ? t : "";
        }
        return Vars::format_scalar(d->type, h.live().v, d->digits, d->str, buf, cap);
    }

    void MonitorPage::_flag_value(Vars::Var h, char* buf, size_t cap) {
        // [O] forced, [I] the reading underneath is not good, [OI] both: two
        // independent facts, so two independent letters.
        const bool o = h.override().present();
        const bool i = !h.live().good();
        if (!o && !i) return;
        char tag[6] = "[";
        size_t n = 1;
        if (o) tag[n++] = 'O';
        if (i) tag[n++] = 'I';
        tag[n++] = ']';
        tag[n] = '\0';
        _prefix(buf, cap, tag);
    }

    void MonitorPage::_describe_binding(const Vars::Binding& b, char* out,
                                        size_t cap) const {
        switch (b.transport) {
            case Vars::XP_MODBUS: {
                // The unit number is what the wire uses; the TAG is what the
                // plant calls the box, so an operator need not look "dev 34"
                // up in a spreadsheet.
                const char* tag = (_device_name != nullptr)
                                      ? _device_name(_device_ctx, b.device) : nullptr;
                if (tag != nullptr && tag[0] != '\0')
                    snprintf(out, cap, "Modbus %s (dev %u) fc %u reg %u",
                             tag, (unsigned)b.device, (unsigned)b.fc, (unsigned)b.reg);
                else
                    snprintf(out, cap, "Modbus dev %u fc %u reg %u",
                             (unsigned)b.device, (unsigned)b.fc, (unsigned)b.reg);
                break;
            }
            case Vars::XP_LOCAL:
                // The wire type is what makes a local channel analog or
                // digital; the transport only says it is not Modbus.
                snprintf(out, cap, "%s channel %u",
                         (b.wire == Vars::T_BOOL) ? "digital" : "analog",
                         (unsigned)b.reg);
                break;
            default:
                snprintf(out, cap, "computed");
                break;
        }
    }

    void MonitorPage::_draw_rules() {
        uint16_t win = _window(), total = _total();
        _draw_rule(TRULE_ROW, _top > 0 ? '^' : '+');
        _draw_rule(_brule_row(), (uint16_t)(_top + win) < total ? 'v' : '+');
    }

    const char* MonitorPage::_selected_cell() const {
        bool ok = false;
        Vars::VarRef ref = _ref_at(_selected, ok);
        if (!ok || _io == nullptr) return nullptr;
        return _io->var(ref).meta()->name;
    }

    void MonitorPage::_draw_panel() {
        uint16_t br = _brule_row();
        _blank_line((uint16_t)(br + 1));    // breathe under the table
        bool ok = false;
        Vars::VarRef ref = _ref_at(_selected, ok);
        const bool have = ok && _io != nullptr;
        Vars::Var h = have ? _io->var(ref) : Vars::Var(nullptr, nullptr);
        _panel_field((uint16_t)(br + 2), "Description", have ? h.description() : nullptr);
        _panel_field((uint16_t)(br + 3), "Notes",       have ? h.notes() : nullptr);
        // "Mapping", not "Source": for an OUTPUT nothing comes FROM the row,
        // it goes there. The field names which point on which device the row
        // is mapped to, in either direction.
        char bind[72];
        if (have) _describe_binding(h.meta()->bind, bind, sizeof(bind));
        _panel_field((uint16_t)(br + 4), "Mapping", have ? bind : nullptr);

        // Quality, plus whichever of the two things qualifies it: for a
        // READING its age (Q_GOOD from four minutes ago is not Q_GOOD from
        // this beat), for a DRIVEN or COMPUTED row the program that wrote it.
        char qual[80];
        if (have) {
            size_t n = (size_t)snprintf(qual, sizeof(qual), "%s",
                                        Vars::quality_str(h.live().q));
            // A force carries its own quality (ovr_valid), so an overridden
            // row says both: the force, and what the row underneath reports.
            if (h.override().present() && n < sizeof(qual))
                n += (size_t)snprintf(qual + n, sizeof(qual) - n, ", forced %s",
                                      h.valid() ? "valid" : "invalid");

            const Vars::VarRole role = h.meta()->role;
            if (role == Vars::ROLE_INPUT && h.stamp() != 0 && n < sizeof(qual)) {
                char age[16];
                const uint64_t now = HAL::Clock::uptime_ms();
                const uint32_t secs = (uint32_t)((now - h.stamp()) / 1000u);
                // format_age already says "ago"; the width is fixed so the
                // field does not reflow as the reading gets older.
                n += (size_t)snprintf(qual + n, sizeof(qual) - n, ", %s",
                                      Utils::format_age(age, sizeof(age), secs));
            } else if (role != Vars::ROLE_INPUT && role != Vars::ROLE_PARAM &&
                       _writer_name != nullptr && n < sizeof(qual)) {
                // Outputs, computed rows and status values alike: name the
                // program that wrote it. No name means no program drives the
                // row, which answers "why is this sitting at its default".
                const char* who = _writer_name(_writer_ctx, _io->writer_of(ref));
                n += (size_t)snprintf(qual + n, sizeof(qual) - n, ", by %s",
                                      who != nullptr ? who : "no program");
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

    void MonitorPage::_say(const char* text) {
        snprintf(_said, sizeof(_said), "%s", text);
        _said_until_ms = HAL::Clock::uptime_ms() + SAID_MS;
        _dirty = true;
    }

    uint16_t MonitorPage::_edit_label(char* buf, size_t cap) const {
        const char* nm = _io->var(_edit_ref).meta()->name;
        int n = snprintf(buf, cap, " %s = ", nm != nullptr ? nm : "?");
        return (n < 0) ? 0 : (uint16_t)n;
    }

    void MonitorPage::_draw_prompt_row() {
        const uint16_t row = _prompt_row();
        if (_editing) {
            _terminal.set_inverted(false);
            _terminal.move_to({row, 0});
            _terminal.clear_to_eol();
            char label[40];
            const uint16_t lw = _edit_label(label, sizeof(label));
            _terminal.write_chars(label, lw);
            _editor.redraw();
            if (_edit_error) _draw_error_marker();
            _terminal.set_cursor_visible(true);
            return;
        }
        _terminal.set_cursor_visible(false);
        const char* say = (HAL::Clock::uptime_ms() < _said_until_ms) ? _said : "";
        _draw_line(row, say, (uint16_t)std::strlen(say));
    }

    void MonitorPage::_draw_error_marker() {
        _terminal.move_to(_editor.end_pos());
        const char* e = "  [invalid]";
        _terminal.write_chars(e, (uint16_t)std::strlen(e));
        _editor.place_cursor();
    }

    void MonitorPage::_clear_error() {
        _terminal.move_to(_editor.end_pos());
        _terminal.clear_to_eol();
        _edit_error = false;
        _editor.place_cursor();
    }

    void MonitorPage::_start_edit() {
        if (_io == nullptr) return;
        bool ok = false;
        const Vars::VarRef ref = _ref_at(_selected, ok);
        if (!ok) return;
        Vars::Var h = _io->var(ref);
        // A T_STR's value lives in the text pool, which an override cannot
        // hold: say so rather than opening a field whose every commit fails.
        if (h.meta()->type == Vars::T_STR) { _say("a text row cannot be forced"); return; }
        _edit_ref = ref;

        char label[40];
        const uint16_t lw = _edit_label(label, sizeof(label));
        const char* empty = "";
        _editor.set_ps1(empty, 0);
        _editor.set_origin(_prompt_row());
        _editor.set_indent(lw);
        _editor.set_single_line(true);
        _editor.reset();                 // clears the row, label included
        _terminal.move_to({_prompt_row(), 0});
        _terminal.write_chars(label, lw);

        // Seeded with what the row reads NOW - a force usually starts from the
        // reading it is replacing.
        char val[OVR_INPUT_LEN];
        const char* v = Vars::var_format(*_io, h, val, sizeof(val));
        const uint16_t n = (uint16_t)std::strlen(v);
        if (v != val) { std::strncpy(val, v, sizeof(val) - 1); val[sizeof(val) - 1] = 0; }
        if (n > 0) _editor.insert_characters(val, n);
        else       _editor.place_cursor();

        _edit_error = false;
        _editing = true;
        _terminal.set_cursor_visible(true);
    }

    void MonitorPage::_cancel_edit() {
        _editing = false;
        _dirty = true;
        _draw_prompt_row();              // hide the cursor, clear the field
    }

    void MonitorPage::_commit_edit() {
        char line[OVR_INPUT_LEN];
        uint16_t n = _editor.buffer_len();
        if (n > (uint16_t)(sizeof(line) - 1)) n = (uint16_t)(sizeof(line) - 1);
        std::memcpy(line, _editor.buffer(), n);
        line[n] = 0;
        if (_io->override_set(_io->var(_edit_ref), line) != Vars::SET_OK) {
            _edit_error = true;          // stay in the field, flag it
            _draw_error_marker();
            return;
        }
        _editing = false;
        _dirty = true;                   // the OVERRIDE column says the rest
        _say("override set - ^S saves this row");
        _draw_prompt_row();
    }

    void MonitorPage::_handle_edit_command(Command cmd, uint16_t count) {
        if (cmd == Command::ENTER) { _commit_edit(); return; }
        if (cmd == Command::ESC)   { _cancel_edit(); return; }
        const EditKey k = _edit_kind(cmd);
        if (k == EDIT_IGNORED) return;
        if (k == EDIT_CHANGED && _edit_error) _clear_error();
        _edit_apply(_editor, cmd, count);
    }

    void MonitorPage::_toggle_validity() {
        if (_io == nullptr) return;
        bool ok = false;
        const Vars::VarRef ref = _ref_at(_selected, ok);
        if (!ok) return;
        Vars::Var h = _io->var(ref);
        if (h.meta()->type == Vars::T_STR) { _say("a text row cannot be forced"); return; }
        // With nothing forced, the row is frozen where it stands and marked
        // dead in one act: "make this sensor read failed" is the reason to
        // reach for this at all, and it would otherwise be ^O then ^V.
        const bool forced = h.override().present();
        const bool valid  = forced ? h.valid() : true;
        _io->override_set(h, h.value().v, !valid);
        if (!forced)     _say("forced, and reading invalid");
        else if (valid)  _say("override now reads invalid");
        else             _say("override now reads valid");
        _dirty = true;
    }

    void MonitorPage::_clear_override() {
        if (_io == nullptr) return;
        bool ok = false;
        const Vars::VarRef ref = _ref_at(_selected, ok);
        if (!ok) return;
        Vars::Var h = _io->var(ref);
        if (!h.override().present()) { _say("not overridden"); return; }
        _io->override_clear(h);          // through the bank: it owns the store
        _say("override released");
    }

    void MonitorPage::_save_override() {
        if (_io == nullptr) return;
        bool ok = false;
        const Vars::VarRef ref = _ref_at(_selected, ok);
        if (!ok) return;
        // The row under the cursor, not every unsaved one: what is being
        // committed to flash should be the row the operator is looking at.
        switch (_io->save_override(_io->var(ref))) {
            case Vars::SET_OK:            _say("override saved");                 break;
            case Vars::SET_NOT_PERSISTED: _say("NOT saved - storage refused it"); break;
            default:                      _say("nothing forced on this row");     break;
        }
        _dirty = true;      // the legend's unsaved count moved
    }
}}

#endif // BOTANIK_FEATURE_MONITOR_PAGE
