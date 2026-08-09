/**
 * @file monitor.cpp
 * @brief MonitorPage implementation - the published-variable table.
 */
#include "libbotanik/pages/monitor.h"

#include <cstdio>                           // snprintf
#include <cstring>                          // strlen / strcmp / memcpy

#include "libbotanik/hal/platform.h"        // HAL::Clock::uptime_ms

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
        // Rebuild on the value cadence (or immediately after a nav): the table
        // is redrawn immediate-mode and the terminal's back-buffer diff emits
        // only the cells that moved, so the page keeps no per-row cache - but
        // re-formatting every row every tick would still burn CPU for nothing.
        uint64_t now = HAL::Clock::uptime_ms();
        if (!_dirty && now - _last_refresh_ms < Config::PAGE_HOT_MS) return;
        _last_refresh_ms = now;
        _dirty = false;
        uint16_t win = _window();
        _clamp_selection();
        _follow_selection();
        _draw_structure();
        _draw_rules();
        // The band goes out BEFORE the panel and footer, which is not
        // cosmetic: both of those resolve _selected, and doing it first
        // parked _ref_at()'s memo past the band's first row - so the band
        // rescanned from the top, and the panel had already paid a scan of
        // its own to get there. Rows first seeds the memo at _top and walks
        // forward; _selected is inside the band, so the two trailing lookups
        // then resume instead of scanning. One walk per refresh, not three.
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
            const Vars::Variable& d = *_io->var(i).meta();
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
        const Vars::Variable& d = *_io->var(i).meta();
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

    uint16_t MonitorPage::_value_w() const {
        return _flex_width((uint16_t)(2 * MARGIN + 2 + SCOPE_W + 3 + NAME_W + 3
                                      + 3 + UNIT_W + 3 + OVR_W + 2),
                           (uint16_t)(VAL_CAP - 1));
    }

    uint8_t MonitorPage::_columns(Col* out) const {
        out[C_SCOPE] = Col{ "SCOPE",    SCOPE_W    };
        out[C_NAME]  = Col{ "VARIABLE", NAME_W     };
        out[C_VALUE] = Col{ "VALUE",    _value_w() };
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
            const Vars::Variable* m = h.meta();
            if (m->name != nullptr) nm = m->name;
            if (m->unit != nullptr) un = m->unit;
            // Scope, not the group: the group is the VIEW now (SPACE/LEFT/
            // RIGHT), so repeating it down a column said the same thing on
            // every row. Where a reading comes from does not.
            sc = Vars::var_scope(*m);
            _adopt(val, sizeof(val), Vars::var_format(*_io, h, val, sizeof(val)));
            if (h.overridden())
                _adopt(ovr, sizeof(ovr), Vars::var_format(*_io, h, ovr, sizeof(ovr)));
        }
        const char* cells[5] = { sc, nm, val, un, ovr };
        return _build_row(out, cap, cells);
    }

    void MonitorPage::_draw_row(uint16_t r, uint16_t row, bool selected) {
        char line[LINE_CAP];
        uint16_t n = _row_text(r, line, sizeof(line));
        _draw_selected_line(row, line, n, _cell_col(C_NAME), _cell_end(C_NAME),
                            selected);
    }

    void MonitorPage::_describe_binding(const Vars::Binding& b, char* out, size_t cap) {
        switch (b.transport) {
            case Vars::XP_MODBUS:
                snprintf(out, cap, "Modbus dev %u fc %u reg %u",
                         (unsigned)b.device, (unsigned)b.fc, (unsigned)b.reg);
                break;
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

    uint16_t MonitorPage::_selected_cell(char* out, uint16_t cap) const {
        bool ok = false;
        Vars::VarRef ref = _ref_at(_selected, ok);
        if (!ok || _io == nullptr) return 0;
        return _copy_cell(out, cap, _io->var(ref).meta()->name);
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
        // The binding, which the byte-offset model had nowhere to put: where
        // this reading physically comes from is the first thing you want
        // when one looks wrong.
        //
        // "Mapping", not "Source": for a relay coil the old label read as "where
        // this value came from", which for an OUTPUT is backwards - nothing
        // comes from it, it goes there. What the field actually says is which
        // point on which device this row is mapped to, in both directions.
        char bind[64];
        if (have) {
            _describe_binding(h.meta()->bind, bind, sizeof(bind));
            // A computed row's mapping is the logic that computes it, so name
            // the program. "computed" alone told an operator only that nothing
            // on the bus explains the number - not where to go and look.
            if (h.meta()->bind.transport == Vars::XP_NONE && _writer_name != nullptr) {
                const uint8_t w = _io->writer_of(ref);
                const char* who = _writer_name(_writer_ctx, w);
                if (who != nullptr) {
                    const size_t n = strlen(bind);
                    snprintf(bind + n, sizeof(bind) - n, " (%s)", who);
                }
            }
        }
        _panel_field((uint16_t)(br + 4), "Mapping", have ? bind : nullptr);
        // Quality is likewise new: a variable nothing has written reads as its
        // default, which looks exactly like a real reading otherwise.
        //
        // Two lines, not one. An override SHADOWS the row rather than replacing
        // it - the driver keeps publishing underneath - so the two facts are
        // independent and an operator needs both: whether the reading is being
        // forced, and whether the thing underneath is healthy. Folding them into
        // one field meant a forced row never showed its own quality, which is
        // exactly what decides whether the override can come off.
        _panel_field((uint16_t)(br + 5), "Quality",
                     have ? Vars::quality_str(h.live_quality()) : nullptr);
        const char* forced = nullptr;
        if (have && h.overridden())
            // valid() IS the forced quality while overridden - that is what
            // ovr_valid means - so this reads the override's own verdict, not
            // the row's underneath.
            forced = h.valid() ? "forced by hand"
                               : "forced, and forced to read invalid";
        _panel_field((uint16_t)(br + 6), "Override", forced);
    }

    void MonitorPage::_draw_footer() {
        // In the "all" view the badge does not say which group the cursor is
        // sitting in, so the legend does; in a single-group view it would
        // just repeat the badge.
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
