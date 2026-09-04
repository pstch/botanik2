/**
 * @file shell_vars.cpp
 * @brief The variable tables: `vars`, `params` and `overrides`, over the shared
 *        row-rendering helpers they all print through.
 */
#include "libbotanik/shell/shell.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_SHELL_PAGE && BOTANIK_FEATURE_VARS
#include "libbotanik/shell/args.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "libbotanik/utils/text.h"
#include "libbotanik/vars/access.h"
#include "libbotanik/vars/persist.h"

#if BOTANIK_FEATURE_PLC
#include "libbotanik/tasks/tasks.h"
#endif

#include "libbotanik/config/logging.h"

namespace Botanik { namespace Shell {

    /** @brief Which edge a pulse row counts, as a word. */
    inline const char* edge_word(uint8_t edge) {
        switch ((HAL::Pulse::Edge)edge) {
            case HAL::Pulse::EDGE_FALLING: return "falling";
            case HAL::Pulse::EDGE_BOTH:    return "either";
            default:                       return "rising";
        }
    }


    void CommandShell::_emit_var(const Vars::Bank& io, Vars::Var h) {
        char out[128], val[48];
        const char* s = Vars::var_format(io, h, val, sizeof(val));
        const char* u = h.meta().unit;
        // Flag the three things a bare number hides: the kind (on every row,
        // since it decides whether `set` or `override` applies), that it is
        // forced by hand, and that a reading is really its default because
        // nothing has written it.
        _emitf(out, sizeof(out), "  %-20s %-12s %-4s (%s)%s%s%s", h.meta().name, s,
               (u && u[0]) ? u : "",
               Vars::kind_label(h.kind()),
               h.override().present() ? " OVERRIDDEN" : "",
               h.param_unsaved() ? " UNSAVED" : "",
               h.valid() ? "" : " (default: nothing has written it)");
    }

    // `vars get`: the value, and nothing that is not the value. One line, so it
    // is read at a glance and scraped by whatever wants field 1; `vars info` is
    // where everything about the row went.
    void CommandShell::_show_value(const char* noun, const char* name,
                                   uint16_t nlen) {
        char out[96], val[64];
        Vars::Bank* io = _bank();
        if (io == nullptr) { _emitf(out, sizeof(out), "%s: no variables", noun); return; }
        if (name == nullptr) {
            _emitf(out, sizeof(out), "usage: %s get <name>   (%s info <name> for the row)",
                   noun, noun);
            return;
        }
        const uint16_t r = io->find(name, nlen);
        if (r == Vars::VAR_NONE) {
            _emitf(out, sizeof(out), "%s: unknown variable", noun); return;
        }
        const Vars::Var h = io->var(r);
        const char* u = h.meta().unit;
        // The value is the one every other reader sees, override applied. Only
        // its QUALITY comes with it: a number a dead sensor is not producing is
        // not a reading, and a bare one would be taken for one.
        _emitf(out, sizeof(out), "%s%s%s%s",
               Vars::var_format(*io, h, val, sizeof(val)),
               (u && u[0]) ? " " : "", (u && u[0]) ? u : "",
               h.valid() ? "" : " INVALID");
    }

    // The detail view: what kind of row, whether you may write it, what it
    // reads underneath any override. Laid out like `stats` and `net` (dotted
    // leader to a fixed column, value, detail in parentheses).
    void CommandShell::_show_var(const char* noun, const char* verb,
                                 const char* name,
                                 uint16_t nlen, bool params_only) {
        char out[Config::LOG_FORMATTED_LEN];
        Vars::Bank* io = _bank();
        if (io == nullptr) { _emitf(out, sizeof(out), "%s: no variables", noun); return; }
        if (name == nullptr) {
            _emitf(out, sizeof(out), "usage: %s %s <name>   (%s list to browse)",
                   noun, verb, noun);
            return;
        }

        const uint16_t r = io->find(name, nlen);
        if (r == Vars::VAR_NONE) {
            _emitf(out, sizeof(out), "%s: unknown variable", noun); return;
        }
        const Vars::Var h = io->var(r);
        const Vars::Descriptor& d = h.meta();
        // `params get` answers about parameters only: pointing at the noun that
        // does own the row beats "unknown", which would send someone looking
        // for a typo that is not there.
        if (params_only && d.kind != Vars::K_PARAM) {
            _emitf(out, sizeof(out), "%s: %s is not a parameter (vars info %s)",
                   noun, d.name, d.name);
            return;
        }

        char val[64], det[80];

        const char* u = (d.unit && d.unit[0]) ? d.unit : "";

        // The name and what every other reader sees for it: the override
        // applied. The rows below take that apart.
        _emitf(out, sizeof(out), "%s = %s %s", d.name,
               Vars::var_format(*io, h, val, sizeof(val)), u);

        // Both derived (the kind is declared, the group is the name's prefix),
        // so neither can disagree with the table.
        _field("Kind", Vars::kind_label(d.kind), _kind_phrase(d));
        _field("Group", Vars::var_group(d), nullptr);
        // Whether `set` works; everything is forceable, so that is the only
        // question.
        // The row's DECLARED level, not this channel's: the console is not
        // levelled, and what an operator wants to know here is which channel
        // could change it.
        _field("Access", Vars::access_label(h.meta()), nullptr);
        // Only on a parameter, and only when it matters: a row that is durable
        // has nothing to say here, and saying it on every row would train the
        // eye to skip the line that does.
        if (d.kind == Vars::K_PARAM && h.param_unsaved())
            _field("Saved", "NO", "edited since the last save");

        // The two values, kept apart: an override SHADOWS (the driver keeps
        // publishing underneath), so "held at what" and "would read what if
        // released" are different questions. The raw row says WHY it is
        // invalid, not just that it is.
        _value_field("Raw value", *io, h, h.live(),
                     Vars::quality_label((uint64_t)h.live().q), u);
        // The override's quality is a person's choice, not a diagnosis: a yes
        // or a no.
        const Vars::Value ovr = h.override();
        if (ovr.present()) {
            char q[32];
            snprintf(q, sizeof(q), "%s%s", ovr.good() ? "valid" : "invalid",
                     h.override_unsaved() ? ", unsaved" : "");
            _value_field("Override value", *io, h, ovr, q, u);
        } else {
            _field("Override value", "-", nullptr);
        }
        // The third value, and a different fact from either: what the row falls
        // back TO, which is not what it reads now and not what is forced on it.
        {
            char num[48], cell[64];
            // Not var_format(): that reads the LIVE text out of the pool, and a
            // T_STR's default is the flash literal the row was declared with.
            const char* text = (d.type == Vars::T_STR)
                ? (d.def.s != nullptr ? d.def.s : "")
                : Vars::format_scalar(d.type, d.def, d.digits, d.labels,
                                      num, sizeof(num));
            snprintf(cell, sizeof(cell), "%s%s%s", text, u[0] ? " " : "", u);
            _field("Default", cell, _default_phrase(d));
        }
        // Only where it is true, like Saved above. Asked of Persist rather than
        // of the flag: F_RETAIN reads two ways and neither is the raw bit - a
        // computed row is written back to the retained store, a PARAMETER is
        // one `params reset` leaves alone. A line here that storage does not
        // keep would be worse than none.
        if (Vars::Persist::retainable(d))
            _field("Retained", "yes", "written back; survives a reboot");
        else if (Vars::Persist::param_retained(d))
            _field("Retained", "yes", "a record; `params reset` leaves it alone");

        // The band, live: an end may name another row, and an analog row's
        // comes from its calibration - Var::band() resolves both.
        const Vars::Band b = h.band();
        if (b.set()) {
            char lo[24], hi[24];
            snprintf(det, sizeof(det), "%s .. %s %s",
                     Vars::format_scalar(d.type, b.lo, d.digits, nullptr, lo, sizeof(lo)),
                     Vars::format_scalar(d.type, b.hi, d.digits, nullptr, hi, sizeof(hi)), u);
            _field(d.kind == Vars::K_PARAM ? "Range" : "Plausible", det, nullptr);
        }

        // Where the value physically comes from.
        _field("Source", _binding_phrase(d, det, sizeof(det)), nullptr);
        // ...and which of the control programs it passes through, which the
        // binding does not answer: a computed row has no wire and still has an
        // author.
        if (d.desc  && d.desc[0])  _field("Description", d.desc,  nullptr);
        if (d.notes && d.notes[0]) _field("Notes",       d.notes, nullptr);
    }

    // The leader is built rather than written into each label, so the column
    // cannot drift one row at a time.

    // Quality rides on the value row rather than one of its own: a value and
    // whether it is good are one fact, and there are two values.
    void CommandShell::_value_field(const char* label, const Vars::Bank& io,
                                    Vars::Var h, Vars::Value val,
                                    const char* quality, const char* unit) {
        const Vars::Descriptor& d = h.meta();
        char num[48], cell[64];
        // A T_STR keeps its live text in the pool, not the slot, so the Scalar
        // is not where its value is; var_format() is the only reader that knows.
        const char* text = (d.type == Vars::T_STR)
            ? Vars::var_format(io, h, num, sizeof(num))
            : Vars::format_scalar(d.type, val.v, d.digits, d.labels, num, sizeof(num));
        snprintf(cell, sizeof(cell), "%s%s%s", text, unit[0] ? " " : "", unit);
        _field(label, cell, quality);
    }

    // `def` does three jobs (@see Descriptor::def), and which one is the whole
    // reason to print it: the number says nothing about what it is for.
    const char* CommandShell::_default_phrase(const Vars::Descriptor& d) {
        // The descriptor, not the role alone: a RETAINED parameter is the one
        // kind whose default nothing puts back, so the phrase would be a lie.
        if (Vars::Persist::param_retained(d))
            return "factory value; retained, so nothing puts it back";
        switch (d.kind) {
            case Vars::K_PARAM:  return "factory value; `params reset` restores it";
            case Vars::K_OUTPUT: return "safe state, driven when nothing drives it";
            case Vars::K_INPUT:  return "substituted while nothing is behind the row";
            default:             return "value before anything computes it";
        }
    }

    // What the one-letter kind means, for the detail column: the table shows
    // the letter because a column cannot spare eight.
    const char* CommandShell::_kind_phrase(const Vars::Descriptor& d) {
        switch (d.kind) {
            case Vars::K_ALARM:   return "alarm condition";
            case Vars::K_PARAM:   return "parameter";
            case Vars::K_QUALITY: return "quality of the row above it";
            case Vars::K_STATUS:
                return Vars::mapped(d) ? "status value, published" : "computed";
            case Vars::K_INPUT:
                return Vars::analog(d) ? "input, calibrated"
                     : Vars::pulse(d)  ? "input, edge count" : "input, direct";
            case Vars::K_OUTPUT:
                return Vars::analog(d) ? "output, scaled" : "output, direct";
        }
        return "?";
    }

    // Mirrors the MONITOR page's Source field, so a row reads the same whichever
    // showed it.
    const char* CommandShell::_binding_phrase(const Vars::Descriptor& d,
                                              char* buf, size_t cap) {
        const Vars::Binding& b = d.bind;
        if (d.kind != Vars::K_INPUT && d.kind != Vars::K_OUTPUT)
            snprintf(buf, cap, "computed (nothing drives it)");
        else if (Vars::pulse(d))
            // A counter, not a level: the row IS the running edge total.
            snprintf(buf, cap, "pulse counter on channel %u, %s edges",
                     (unsigned)Vars::pulse_channel(d), edge_word(Vars::pulse_edge(d)));
        else if (Vars::local(d))
            // The wire type is what makes a local channel analog or digital;
            // the device address only says it is not on the bus.
            snprintf(buf, cap, "%s channel %u",
                     (b.wire == Vars::T_BOOL) ? "digital" : "analog",
                     (unsigned)b.reg);
        else
            snprintf(buf, cap, "Modbus dev %u fc %u reg %u",
                     (unsigned)b.device, (unsigned)b.fc, (unsigned)b.reg);
        return buf;
    }

    void CommandShell::_list_vars(const char* noun, const char* prefix,
                                  uint16_t plen, bool params_only) {
        Vars::Bank* io = _bank();
        char out[96];
        if (io == nullptr) { _emitf(out, sizeof(out), "%s: no variables", noun); return; }

        // Variables are named by domain (`pwr_`, `env_`), so a prefix is the
        // subsystem view without a grouping concept in the command.
        uint16_t n = 0;
        for (uint16_t i = 0; i < io->count(); i++) {
            Vars::Var h = io->var(i);
            const char* name = h.meta().name;
            if (name == nullptr) continue;
            if (params_only && h.kind() != Vars::K_PARAM) continue;
            if (!Utils::glob_bounded(name, prefix, plen)) continue;
            _emit_var(*io, h);
            n++;
        }
        if (n == 0) { _emitf(out, sizeof(out), "%s: nothing matches", noun); return; }
        _emitf(out, sizeof(out), "  %u %s", n, params_only ? "parameter(s)" : "variable(s)");
    }

    void CommandShell::_cmd_vars(const char* args, uint16_t alen) {
        uint16_t wv; const char* verb = arg_next(args, alen, wv);
        uint16_t wa; const char* a    = arg_next(args, alen, wa);

        if (arg_eq(verb, wv, "list")) {
            _list_vars("vars", a, wa, /*params_only=*/false);
            return;
        }
        if (arg_eq(verb, wv, "get")) {
            _show_value("vars", a, wa);
            return;
        }
        if (arg_eq(verb, wv, "info")) {
            _show_var("vars", "info", a, wa, /*params_only=*/false);
            return;
        }
        _verb_usage("vars");
    }

    void CommandShell::_cmd_params(const char* args, uint16_t alen) {
        Vars::Bank* io = _bank();
        char out[128];
        uint16_t wv; const char* verb = arg_next(args, alen, wv);
        uint16_t wn; const char* nm   = arg_next(args, alen, wn);

        if (arg_eq(verb, wv, "list")) {
            _list_vars("params", nm, wn, /*params_only=*/true);
            // The one thing a listing must not hide: an edit nobody committed.
            if (io != nullptr) {
                const uint16_t pending = io->unsaved_count();
                if (pending != 0)
                    _emitf(out, sizeof(out),
                           "  %u unsaved (`params save all` to commit)", pending);
            }
            return;
        }
        if (arg_eq(verb, wv, "get")) {
            _show_var("params", "get", nm, wn, /*params_only=*/true);
            return;
        }
        if (arg_eq(verb, wv, "set"))   { _params_set(nm, wn, args, alen); return; }
        if (arg_eq(verb, wv, "save"))  { _params_save(nm, wn);  return; }
        if (arg_eq(verb, wv, "clear")) { _params_clear(nm, wn); return; }
        if (arg_eq(verb, wv, "reset")) { _params_reset(nm, wn); return; }

        _verb_usage("params");
    }

    // Back to the factory default, live and durably - the stored copy goes too,
    // or the next boot would undo it. `clear` is the lighter one below.
    void CommandShell::_params_reset(const char* nm, uint16_t wn) {
        Vars::Persist* st = _store();
        if (st == nullptr) { _println("params: nothing to save to"); return; }
        Vars::Bank* io = _bank();
        char out[128];
        if (io == nullptr) { _println("params: no variables"); return; }

        if (nm == nullptr) { _verb_usage("params", "reset"); return; }

        if (arg_is_all(nm, wn)) {
            uint16_t done = 0, refused = 0, kept = 0;
            for (uint16_t k = 0; k < io->param_count(); k++) {
                Vars::Var p = io->param_at(k);
                if (Vars::Persist::param_retained(p.meta())) { kept++; continue; }
                switch (st->reset(p)) {
                    case Vars::SET_OK:             done++;    break;
                    case Vars::SET_NOT_PERSISTED:  refused++; break;
                    default: break;                // read-only rows are not errors
                }
            }
            if (refused != 0)
                _emitf(out, sizeof(out), "params: %u reset, %u REFUSED by storage",
                       done, refused);
            else
                _emitf(out, sizeof(out), "params: %u reset to default", done);
            if (kept != 0)
                _emitf(out, sizeof(out), "  %u retained, left alone", kept);
            return;
        }

        const uint16_t r = io->find(nm, wn);
        if (r == Vars::VAR_NONE) { _println("params: unknown parameter"); return; }
        Vars::Var p = io->var(r);
        if (Vars::Persist::param_retained(p.meta())) {
            _emitf(out, sizeof(out), "%s is retained: `params reset` leaves it alone",
                   p.meta().name);
            _println("  it records what happened; `params set` is how it moves");
            return;
        }
        switch (st->reset(p)) {
            case Vars::SET_OK:
                _emitf(out, sizeof(out), "%s reset to its default", p.meta().name);
                break;
            case Vars::SET_NOT_PERSISTED:
                _emitf(out, sizeof(out),
                       "%s reset, but storage kept the saved copy", p.meta().name);
                break;
            default:
                _emitf(out, sizeof(out), "params: %s is not an editable parameter",
                       p.meta().name);
                break;
        }
    }

    // Walk back an edit that was never committed: the row goes to what a reboot
    // would give it and the stored copy is left alone. `params reset` is the
    // destructive one - this is the undo.
    void CommandShell::_params_clear(const char* nm, uint16_t wn) {
        Vars::Persist* st = _store();
        if (st == nullptr) { _println("params: nothing to save to"); return; }
        Vars::Bank* io = _bank();
        char out[128];
        if (io == nullptr) { _println("params: no variables"); return; }
        if (nm == nullptr) { _verb_usage("params", "clear"); return; }

        if (arg_is_all(nm, wn)) {
            uint16_t done = 0;
            for (uint16_t k = 0; k < io->param_count(); k++) {
                const Vars::Var p = io->param_at(k);
                // Count what actually had something outstanding, not every row
                // walked: "142 cleared" after editing one is a lie.
                if (!p.param_unsaved()) continue;
                if (st->revert(p) == Vars::SET_OK) done++;
            }
            _emitf(out, sizeof(out), "params: %u unsaved edit%s forgotten",
                   done, done == 1 ? "" : "s");
            return;
        }

        const uint16_t r = io->find(nm, wn);
        if (r == Vars::VAR_NONE) { _println("params: unknown parameter"); return; }
        Vars::Var p = io->var(r);
        const bool had = p.param_unsaved();
        switch (st->revert(p)) {
            case Vars::SET_OK:
                _emitf(out, sizeof(out), had ? "%s back to its saved value"
                                             : "%s had no unsaved edit",
                       p.meta().name);
                break;
            default:
                _emitf(out, sizeof(out), "params: %s is not an editable parameter",
                       p.meta().name);
                break;
        }
    }

    // An edit is LIVE but not durable: the value is on the plant the moment
    // this returns, and `params save` is what makes it survive a reboot. Saying
    // "(unsaved)" on every write is what keeps the two apart in the operator's
    // head, since nothing else on screen distinguishes them.
    void CommandShell::_params_set(const char* nm, uint16_t wn,
                                   const char* args, uint16_t alen) {
        Vars::Bank* io = _bank();
        char out[128];
        if (io == nullptr) { _println("params: no variables"); return; }
        uint16_t wv; const char* v = arg_next(args, alen, wv);
        if (nm == nullptr || v == nullptr) {
            _verb_usage("params", "set"); return;
        }
        const uint16_t r = io->find(nm, wn);
        if (r == Vars::VAR_NONE) { _println("params: unknown parameter"); return; }
        Vars::Var p = io->var(r);
        // Point at `overrides` rather than failing flatly: the next scan would
        // put a value written here straight back.
        if (p.kind() != Vars::K_PARAM) {
            _emitf(out, sizeof(out),
                   "params: %s is not a parameter (overrides set %s <value> to force it)",
                   p.meta().name, p.meta().name);
            return;
        }

        char val[96];
        uint16_t vc = std::min(wv, (uint16_t)(sizeof(val) - 1));
        memcpy(val, v, vc); val[vc] = '\0';

        if (p.param_set(val) != Vars::SET_OK) {
            _emitf(out, sizeof(out), "params: invalid value for %s", p.meta().name);
            return;
        }
        _emitf(out, sizeof(out), "%s = %s  (unsaved: `params save %s`)",
               p.meta().name, val, p.meta().name);
    }

    void CommandShell::_params_save(const char* nm, uint16_t wn) {
        Vars::Persist* st = _store();
        if (st == nullptr) { _println("params: nothing to save to"); return; }
        Vars::Bank* io = _bank();
        char out[128];
        if (io == nullptr) { _println("params: no variables"); return; }

        if (nm == nullptr) { _verb_usage("params", "save"); return; }

        if (arg_is_all(nm, wn)) {
            uint16_t refused = 0;
            const uint16_t saved = st->save_all(&refused);
            if (saved == 0 && refused == 0) { _println("params: nothing to save"); return; }
            if (refused != 0)
                _emitf(out, sizeof(out), "params: %u saved, %u REFUSED by storage",
                       saved, refused);
            else
                _emitf(out, sizeof(out), "params: %u saved", saved);
            return;
        }

        const uint16_t r = io->find(nm, wn);
        if (r == Vars::VAR_NONE) { _println("params: unknown parameter"); return; }
        Vars::Var p = io->var(r);
        if (p.kind() != Vars::K_PARAM) {
            _emitf(out, sizeof(out), "params: %s is not a parameter", p.meta().name);
            return;
        }
        switch (st->save(p)) {
            case Vars::SET_OK:
                _emitf(out, sizeof(out), "%s saved", p.meta().name);
                break;
            case Vars::SET_NOT_PERSISTED:
                // The value stays live and stays marked, so the next `params
                // save` tries again: say which of the two failed, the fixes are
                // different.
                _emitf(out, sizeof(out), "%s NOT saved: storage refused it",
                       p.meta().name);
                break;
            default:
                _emitf(out, sizeof(out), "params: cannot save %s", p.meta().name);
                break;
        }
    }

    // Shared by every table verb: one lookup, one message.
    uint16_t CommandShell::_resolve(const char* noun, const char* nm,
                                        uint16_t wn) {
        Vars::Bank* io = _bank();
        char err[96];
        const uint16_t none = { Vars::VAR_NONE };
        if (io == nullptr) { _emitf(err, sizeof(err), "%s: no variables", noun); return none; }
        const uint16_t r = io->find(nm, wn);
        if (r == Vars::VAR_NONE) _emitf(err, sizeof(err), "%s: unknown variable", noun);
        return r;
    }

    void CommandShell::_cmd_overrides(const char* args, uint16_t alen) {
        if (_bank() == nullptr) { _println("overrides: no variables"); return; }

        uint16_t wv; const char* verb = arg_next(args, alen, wv);
        if (arg_eq(verb, wv, "list")) { _overrides_list(); return; }

        uint16_t wn; const char* nm = arg_next(args, alen, wn);
        if (arg_eq(verb, wv, "get"))   { _show_var("overrides", "get", nm, wn, false); return; }
        if (arg_eq(verb, wv, "save"))  { _overrides_save(nm, wn);  return; }
        if (arg_eq(verb, wv, "clear")) { _overrides_clear(nm, wn); return; }
        if (arg_eq(verb, wv, "set"))   { _overrides_set(nm, wn, args, alen); return; }

        _verb_usage("overrides");
    }

    // An override outlives the session that set it once saved, so what is forced
    // has to be one command away.
    void CommandShell::_overrides_list() {
        Vars::Bank* io = _bank();
        char out[128], val[48];
        uint16_t n = 0, unsaved = 0;
        for (uint16_t i = 0; i < io->count(); i++) {
            Vars::Var h = io->var(i);
            if (!h.override().present()) continue;
            if (h.override_unsaved()) unsaved++;
            _emitf(out, sizeof(out), "  %-20s %-12s %s%s", h.meta().name,
                   Vars::var_format(*io, h, val, sizeof(val)),
                   h.valid() ? "" : "(reading as invalid) ",
                   h.override_unsaved() ? "(unsaved)" : "");
            n++;
        }
        if (n == 0) { _println("overrides: none active"); return; }
        _emitf(out, sizeof(out),
               "%u override(s) active, %u unsaved (`overrides clear all` to release)",
               n, unsaved);
    }

    void CommandShell::_overrides_clear(const char* nm, uint16_t wn) {
        Vars::Persist* st = _store();
        if (st == nullptr) { _println("overrides: nothing to save to"); return; }
        Vars::Bank* io = _bank();
        char out[128];
        if (nm == nullptr) { _verb_usage("overrides", "clear"); return; }

        if (arg_is_all(nm, wn)) {
            _emitf(out, sizeof(out), "overrides: cleared %u", st->override_clear_all());
            return;
        }
        const uint16_t r = _resolve("overrides", nm, wn);
        if (r == Vars::VAR_NONE) return;
        Vars::Var h = io->var(r);
        // Through Persist, not the handle: the handle cannot reach the store,
        // and a release leaving the stored copy behind would come back at the
        // next boot.
        st->override_clear(h);
        _emitf(out, sizeof(out), "%s: override cleared", h.meta().name);
    }

    void CommandShell::_overrides_set(const char* nm, uint16_t wn,
                                      const char* args, uint16_t alen) {
        Vars::Persist* st = _store();
        if (st == nullptr) { _println("overrides: nothing to save to"); return; }
        Vars::Bank* io = _bank();
        char out[128];
        if (nm == nullptr) {
            _verb_usage("overrides", "set");
            return;
        }
        const uint16_t r = _resolve("overrides", nm, wn);
        if (r == Vars::VAR_NONE) return;
        Vars::Var h = io->var(r);

        uint16_t wa; const char* a = arg_next(args, alen, wa);

        // `set <name> off` is the older spelling for a release, and it reads
        // naturally next to a value; `on` is optional sugar before one.
        if (a != nullptr && arg_eq(a, wa, "off")) {
            st->override_clear(h);
            _emitf(out, sizeof(out), "%s: override cleared", h.meta().name);
            return;
        }
        if (a != nullptr && arg_eq(a, wa, "on")) a = arg_next(args, alen, wa);

        // A trailing `valid` / `invalid` forces the row's `_qlt` companion:
        // "invalid" stands a variable up as a dead sensor without touching what
        // it reports. Accepted either side of the value.
        bool valid = true;
        bool quality = arg_validity(a, wa, valid);
        if (quality) a = arg_next(args, alen, wa);

        char text[64];
        const char* value = nullptr;
        if (a != nullptr) {
            const uint16_t vc = std::min(wa, (uint16_t)(sizeof(text) - 1));
            memcpy(text, a, vc); text[vc] = '\0';
            value = text;
            // Sequenced deliberately: arg_next() WRITES wt, so passing both to
            // one call leaves the read of wt unordered against it.
            uint16_t wt = 0;
            const char* t = arg_next(args, alen, wt);
            if (arg_validity(t, wt, valid)) quality = true;
        }

        if (value != nullptr) {
            if (h.override_set(value) != Vars::SET_OK) {
                _emitf(out, sizeof(out), "overrides: invalid value for %s", h.meta().name);
                return;
            }
        } else if (!quality) {
            h.override_set(h.value().v);   // no value: hold it where it is
        }

        if (quality && !_force_quality(r, valid)) {
            _emitf(out, sizeof(out), "overrides: %s carries no quality to force",
                   h.meta().name);
            return;
        }
        _override_reply(h, value != nullptr ? "forced to"
                                            : (quality ? "reading" : "frozen at"));
    }

    bool CommandShell::_force_quality(uint16_t r, bool valid) {
        const Vars::Var q = _bank()->var(r).quality_var();
        if (q.null()) return false;
        q.override_set(Vars::Scalar::U(valid ? Vars::Q_GOOD : Vars::Q_NONE));
        return true;
    }

    void CommandShell::_override_reply(Vars::Var h, const char* verb) {
        Vars::Bank* io = _bank();
        char out[128], val[48];
        _emitf(out, sizeof(out), "%s: %s %s%s",
               h.meta().name, verb, Vars::var_format(*io, h, val, sizeof(val)),
               h.valid() ? "" : " (reading as invalid)");
    }

    // An override is live the moment it is taken and survives a reboot only
    // once saved. That is deliberate: forcing a signal to chase a fault is the
    // common case, and it should not outlive the session by default - while an
    // override left in place ON PURPOSE (a failed sensor bypassed until the
    // spare arrives) is exactly what wants committing.
    void CommandShell::_overrides_save(const char* nm, uint16_t wn) {
        Vars::Persist* st = _store();
        if (st == nullptr) { _println("overrides: nothing to save to"); return; }
        Vars::Bank* io = _bank();
        char out[128];
        if (io == nullptr) { _println("overrides: no variables"); return; }

        if (nm == nullptr) { _println("usage: overrides save <name>|all"); return; }

        if (arg_is_all(nm, wn)) {
            uint16_t refused = 0;
            const uint16_t saved = st->save_overrides(&refused);
            if (saved == 0 && refused == 0) { _println("overrides: nothing to save"); return; }
            if (refused != 0)
                _emitf(out, sizeof(out), "overrides: %u saved, %u REFUSED by storage",
                       saved, refused);
            else
                _emitf(out, sizeof(out), "overrides: %u saved", saved);
            return;
        }

        const uint16_t r = io->find(nm, wn);
        if (r == Vars::VAR_NONE) { _println("overrides: unknown variable"); return; }
        Vars::Var h = io->var(r);
        switch (st->save_override(h)) {
            case Vars::SET_OK:
                _emitf(out, sizeof(out), "%s: override saved", h.meta().name);
                break;
            case Vars::SET_NOT_PERSISTED:
                _emitf(out, sizeof(out), "%s: override NOT saved: storage refused it",
                       h.meta().name);
                break;
            default:
                _emitf(out, sizeof(out), "%s: nothing forced on it", h.meta().name);
                break;
        }
    }

}}  // namespace Botanik::Shell

#endif // BOTANIK_FEATURE_SHELL_PAGE && BOTANIK_FEATURE_VARS
