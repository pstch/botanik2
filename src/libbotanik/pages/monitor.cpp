/**
 * @file monitor.cpp
 * @brief MonitorPage implementation - the published-variable table.
 */
#include "libbotanik/pages/monitor.h"

#include <cstdio>
#include <cstring>

#include "libbotanik/hal/platform.h"
#include "libbotanik/utils/text.h"

namespace Botanik { namespace Pages {

    void MonitorPage::focus() {
        // The footer carries the group badge, which acknowledges a keypress,
        // so it must beat the table rows it sits under.
        _claim_mode_row(_footer_row());
        _clamp_selection();
        _follow_selection();
        _dirty = true;
    }

    void MonitorPage::tick(bool active) {
        if (!active) return;
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
    }

    void MonitorPage::_ensure_groups() const {
        if (_groups_built || _io == nullptr) return;
        _groups_built = true;
        for (uint16_t i = 0; i < _io->count() && _group_n < Vars::VAR_GROUPS_MAX; i++) {
            const Vars::Descriptor& d = *_io->var(i).meta();
            if (!Vars::var_published(d)) continue;
            const char* g = Vars::var_group(d);
            bool seen = false;
            for (uint8_t k = 0; k < _group_n; k++)
                if (std::strcmp(_group_name[k], g) == 0) { seen = true; break; }
            if (!seen) _group_name[_group_n++] = g;
        }
    }

    const char* MonitorPage::_mode_label(uint8_t i) const {
        _ensure_groups();
        if (i == 0 || i > _group_n) return "all";
        return _group_name[i - 1];
    }

    bool MonitorPage::_in_mode(uint16_t i) const {
        const Vars::Descriptor& d = *_io->var(i).meta();
        if (!Vars::var_published(d)) return false;
        if (_mode == 0) return true;
        // Both sides come out of group_intern(), so one pointer per distinct
        // group: identity IS equality here, and it costs a compare instead
        // of a strcmp.
        return Vars::var_group(d) == _mode_label(_mode);
    }

    uint16_t MonitorPage::_total() const {
        if (_io == nullptr) return 0;
        if (_total_mode == _mode) return _total_n;
        uint16_t t = 0;
        for (uint16_t i = 0; i < _io->count(); i++) if (_in_mode(i)) t++;
        _total_n = t;
        _total_mode = _mode;
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
                if (!_in_mode(i)) continue;
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
            // Scope, not the group: the group is the VIEW, so a group column
            // would say the same thing on every row.
            sc = Vars::var_scope(*m);
            // The LIVE value, not the forced one - the OVERRIDE column shows
            // the force. Printing it in both hides what the operator is
            // deciding on: whether the reading underneath has come back.
            _adopt(val, sizeof(val), _live_text(h, val, sizeof(val)));
            _flag_value(h, val, sizeof(val));
            if (h.overridden()) {
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
            const char* t = _io->text_at(h);
            return (t != nullptr) ? t : "";
        }
        return Vars::format_scalar(d->type, h.live(), d->digits, d->str, buf, cap);
    }

    void MonitorPage::_flag_value(Vars::Var h, char* buf, size_t cap) {
        // [O] forced, [I] the reading underneath is not good, [OI] both: two
        // independent facts, so two independent letters.
        const bool o = h.overridden();
        const bool i = !h.live_valid();
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
        _panel_field((uint16_t)(br + 2), "Description", have ? h.meta()->desc : nullptr);
        _panel_field((uint16_t)(br + 3), "Notes",       have ? h.meta()->notes : nullptr);
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
                                        Vars::quality_str(h.live_quality()));
            // A force carries its own quality (ovr_valid), so an overridden
            // row says both: the force, and what the row underneath reports.
            if (h.overridden() && n < sizeof(qual))
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
        // Only in the "all" view: there the badge cannot say which group the
        // cursor sits in, elsewhere it would just repeat the badge.
        const char* grp = nullptr;
        if (_mode == 0) {
            bool ok = false;
            const Vars::VarRef r = _ref_at(_selected, ok);
            if (ok && _io != nullptr) grp = Vars::var_group(*_io->var(r).meta());
        }
        _draw_mode_line(_footer_row(), grp, nullptr);
        _blank_line((uint16_t)(_footer_row() + 1));   // blank below the legend
    }
}}
