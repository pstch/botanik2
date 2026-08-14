/**
 * @file shell_vars.cpp
 * @brief The variable tables: `vars`, `params` and `overrides`, over the shared
 *        row-rendering helpers they all print through.
 */
#include "libbotanik/shell/shell.h"         // CommandShell
#include "libbotanik/shell/args.h"          // arg_next / arg_eq / arg_is_all

#include <algorithm>                        // std::min
#include <cstdio>                           // snprintf
#include <cstring>                          // strlen / strncmp / memcpy

#include "libbotanik/utils/text.h"          // Utils::format_age / format_float
#include "libbotanik/vars/access.h"         // Vars::var_format / var_group
#include "libbotanik/vars/params.h"         // Vars::Params

#include "libbotanik/config/logging.h"      // Config::LOG_FORMATTED_LEN

namespace Botanik { namespace Shell {

    void CommandShell::_emit_var(const Vars::Bank& io, Vars::Var h) {
        char out[128], val[48];
        const char* s = Vars::var_format(io, h, val, sizeof(val));
        const char* u = h.meta()->unit;
        // Flag the three things a bare number hides: the scope (on every row,
        // since it decides whether `set` or `override` applies), that it is
        // forced by hand, and that a reading is really its default because
        // nothing has written it.
        _emitf(out, sizeof(out), "  %-20s %-12s %-4s (%s)%s%s%s", h.meta()->name, s,
               (u && u[0]) ? u : "",
               Vars::var_scope(*h.meta()),
               h.overridden() ? " OVERRIDDEN" : "",
               h.param_unsaved() ? " UNSAVED" : "",
               h.valid() ? "" : " (default: nothing has written it)");
    }

    // The detail view: what kind of row, whether you may write it, what it
    // reads underneath any override. Laid out like `stats` and `net` (dotted
    // leader to a fixed column, value, detail in parentheses).
    void CommandShell::_show_var(const char* noun, const char* name,
                                 uint16_t nlen, bool params_only) {
        char out[Config::LOG_FORMATTED_LEN];
        Vars::Bank* io = _bank();
        if (io == nullptr) { _emitf(out, sizeof(out), "%s: no variables", noun); return; }
        if (name == nullptr) {
            _emitf(out, sizeof(out), "usage: %s get <name>   (%s list [<prefix>] to browse)",
                   noun, noun);
            return;
        }

        const Vars::VarRef r = io->find(name, nlen);
        if (r.index == Vars::VAR_NONE) {
            _emitf(out, sizeof(out), "%s: unknown variable", noun); return;
        }
        const Vars::Var h = io->var(r);
        const Vars::Variable* d = h.meta();
        // `params get` answers about parameters only: pointing at the noun that
        // does own the row beats "unknown", which would send someone looking
        // for a typo that is not there.
        if (params_only && d->role != Vars::ROLE_PARAM) {
            _emitf(out, sizeof(out), "%s: %s is not a parameter (vars get %s)",
                   noun, d->name, d->name);
            return;
        }

        char val[64], det[80];

        const char* u = (d->unit && d->unit[0]) ? d->unit : "";

        // The name and what every other reader sees for it: the override
        // applied. The rows below take that apart.
        _emitf(out, sizeof(out), "%s = %s %s", d->name,
               Vars::var_format(*io, h, val, sizeof(val)), u);

        // Both derived (scope from the factory that declared it, group from the
        // name), so neither can disagree with the table.
        _field("Scope", Vars::var_scope(*d), _scope_phrase(d->scope));
        _field("Group", Vars::var_group(*d), nullptr);
        // Whether `set` works; everything is forceable, so that is the only
        // question.
        _field("Access", _writable(h) ? "read-write" : "read-only", nullptr);
        // Only on a parameter, and only when it matters: a row that is durable
        // has nothing to say here, and saying it on every row would train the
        // eye to skip the line that does.
        if (d->role == Vars::ROLE_PARAM && h.param_unsaved())
            _field("Saved", "NO", "edited since the last save");

        // The two values, kept apart: an override SHADOWS (the driver keeps
        // publishing underneath), so "held at what" and "would read what if
        // released" are different questions. The raw row says WHY it is
        // invalid, not just that it is.
        _value_field("Raw value", *io, h, h.live(), Vars::quality_str(h.live_quality()), u);
        // The override's quality is a person's choice, not a diagnosis: a yes
        // or a no.
        if (h.overridden()) {
            char q[32];
            snprintf(q, sizeof(q), "%s%s", h.valid() ? "valid" : "invalid",
                     h.override_unsaved() ? ", unsaved" : "");
            _value_field("Override value", *io, h, h.raw(), q, u);
        } else {
            _field("Override value", "-", nullptr);
        }

        // The declared band, live: an analog row's comes from its calibration
        // parameters, so var_band() rather than the flash constants.
        if ((d->flags & Vars::F_BOUNDED) != 0 && d->type != Vars::T_STR) {
            char lo[24], hi[24];
            const Vars::NumBand b = Vars::var_band(io, *d);
            snprintf(det, sizeof(det), "%s .. %s %s",
                     Vars::format_scalar(d->type, b.min, d->digits, nullptr, lo, sizeof(lo)),
                     Vars::format_scalar(d->type, b.max, d->digits, nullptr, hi, sizeof(hi)), u);
            _field(d->role == Vars::ROLE_PARAM ? "Range" : "Plausible", det, nullptr);
        }

        // Where the value physically comes from.
        _field("Source", _binding_phrase(d->bind, det, sizeof(det)), nullptr);
        if (d->desc  && d->desc[0])  _field("Description", d->desc,  nullptr);
        if (d->notes && d->notes[0]) _field("Notes",       d->notes, nullptr);
    }

    // The leader is built rather than written into each label, so the column
    // cannot drift one row at a time.
    void CommandShell::_field(const char* label, const char* value,
                              const char* detail) {
        char lead[LEADER_W + 1];
        size_t n = 0;
        while (label[n] != 0 && n + 1 < sizeof(lead)) { lead[n] = label[n]; n++; }
        if (n + 1 < sizeof(lead)) lead[n++] = ' ';
        while (n + 1 < sizeof(lead)) lead[n++] = '.';
        lead[n] = 0;

        // The detail is cut to what is left of the line rather than allowed to
        // wrap: these reports are read as a column of labels, and one wrapped
        // row breaks the column for every row under it. A task or variable name
        // is site-defined and unbounded, so the width has to be measured here
        // rather than assumed when the format string was written.
        char out[Config::LOG_FORMATTED_LEN];
        if (detail == nullptr || detail[0] == 0) {
            _emitf(out, sizeof(out), "%s : %s", lead, value);
            return;
        }
        size_t vlen = 0;
        while (value[vlen] != 0) vlen++;
        if (vlen < 8) vlen = 8;                       // the %-8s floor
        const size_t cols = _terminal.cols() ? _terminal.cols() : 80;
        const size_t head = LEADER_W + 3 + vlen + 3;  // lead, " : ", value, " ()"
        if (cols <= head) { _emitf(out, sizeof(out), "%s : %-8s", lead, value); return; }
        _emitf(out, sizeof(out), "%s : %-8s (%.*s)", lead, value,
               (int)(cols - head), detail);
    }

    // Quality rides on the value row rather than one of its own: a value and
    // whether it is good are one fact, and there are two values.
    void CommandShell::_value_field(const char* label, const Vars::Bank& io,
                                    Vars::Var h, Vars::Scalar v,
                                    const char* quality, const char* unit) {
        const Vars::Variable* d = h.meta();
        char num[48], cell[64];
        // A T_STR keeps its live text in the pool, not the slot, so the Scalar
        // is not where its value is; var_format() is the only reader that knows.
        const char* text = (d->type == Vars::T_STR)
            ? Vars::var_format(io, h, num, sizeof(num))
            : Vars::format_scalar(d->type, v, d->digits, d->str, num, sizeof(num));
        snprintf(cell, sizeof(cell), "%s%s%s", text, unit[0] ? " " : "", unit);
        _field(label, cell, quality);
    }

    // What a scope abbreviation means, for the detail column: the table shows
    // the two-character code because a column cannot spare eight.
    const char* CommandShell::_scope_phrase(uint8_t scope) {
        switch ((Vars::VarScope)scope) {
            case Vars::SCOPE_A:  return "alarm condition";
            case Vars::SCOPE_P:  return "parameter";
            case Vars::SCOPE_G:  return "computed";
            case Vars::SCOPE_SV: return "status value, published";
            case Vars::SCOPE_MI: return "input, direct";
            case Vars::SCOPE_AI: return "input, calibrated";
            case Vars::SCOPE_DI: return "input, digital";
            case Vars::SCOPE_MO: return "output, direct";
            case Vars::SCOPE_AO: return "output, scaled";
            case Vars::SCOPE_DO: return "output, digital";
            case Vars::SCOPE_RO: return "relay coil";
            default:             return "?";
        }
    }

    // A parameter, unless the site marked it read-only; everything else is
    // driven by something and would be overwritten on the next scan.
    bool CommandShell::_writable(Vars::Var h) {
        const Vars::Variable* d = h.meta();
        return d->role == Vars::ROLE_PARAM && (d->flags & Vars::F_RDONLY) == 0;
    }

    // Mirrors the MONITOR page's Source field, so a row reads the same whichever
    // showed it.
    const char* CommandShell::_binding_phrase(const Vars::Binding& b,
                                              char* buf, size_t cap) {
        switch (b.transport) {
            case Vars::XP_MODBUS:
                snprintf(buf, cap, "Modbus dev %u fc %u reg %u",
                         (unsigned)b.device, (unsigned)b.fc, (unsigned)b.reg);
                break;
            case Vars::XP_LOCAL:
                // The wire type is what makes a local channel analog or digital;
                // the transport only says it is not Modbus.
                snprintf(buf, cap, "%s channel %u",
                         (b.wire == Vars::T_BOOL) ? "digital" : "analog",
                         (unsigned)b.reg);
                break;
            default:
                snprintf(buf, cap, "computed (nothing drives it)");
                break;
        }
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
            const char* name = h.meta()->name;
            if (name == nullptr) continue;
            if (params_only && h.role() != Vars::ROLE_PARAM) continue;
            if (prefix != nullptr && strncmp(name, prefix, plen) != 0) continue;
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
            _show_var("vars", a, wa, /*params_only=*/false);
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
            _show_var("params", nm, wn, /*params_only=*/true);
            return;
        }
        if (arg_eq(verb, wv, "set"))   { _params_set(nm, wn, args, alen); return; }
        if (arg_eq(verb, wv, "save"))  { _params_save(nm, wn);  return; }
        if (arg_eq(verb, wv, "clear")) { _params_clear(nm, wn); return; }

        _verb_usage("params");
    }

    // Back to the factory default, live and durably - the stored copy goes too,
    // or the next boot would undo it.
    void CommandShell::_params_clear(const char* nm, uint16_t wn) {
        Vars::Bank* io = _bank();
        char out[128];
        if (io == nullptr) { _println("params: no variables"); return; }

        if (nm == nullptr) { _println("usage: params clear <name>|all"); return; }

        if (arg_is_all(nm, wn)) {
            uint16_t done = 0, refused = 0;
            for (uint16_t k = 0; k < io->param_count(); k++) {
                switch (io->clear_param(io->param(k))) {
                    case Vars::SET_OK:             done++;    break;
                    case Vars::SET_NOT_PERSISTED:  refused++; break;
                    default: break;                // read-only rows are not errors
                }
            }
            if (refused != 0)
                _emitf(out, sizeof(out), "params: %u cleared, %u REFUSED by storage",
                       done, refused);
            else
                _emitf(out, sizeof(out), "params: %u cleared", done);
            return;
        }

        const Vars::VarRef r = io->find(nm, wn);
        if (r.index == Vars::VAR_NONE) { _println("params: unknown parameter"); return; }
        Vars::Var p = io->var(r);
        switch (io->clear_param(p)) {
            case Vars::SET_OK:
                _emitf(out, sizeof(out), "%s cleared to its default", p.meta()->name);
                break;
            case Vars::SET_NOT_PERSISTED:
                _emitf(out, sizeof(out),
                       "%s reset, but storage kept the saved copy", p.meta()->name);
                break;
            default:
                _emitf(out, sizeof(out), "params: %s is not an editable parameter",
                       p.meta()->name);
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
            _println("usage: params set <name> <value>"); return;
        }
        const Vars::VarRef r = io->find(nm, wn);
        if (r.index == Vars::VAR_NONE) { _println("params: unknown parameter"); return; }
        Vars::Var p = io->var(r);
        // Point at `overrides` rather than failing flatly: the next scan would
        // put a value written here straight back.
        if (p.role() != Vars::ROLE_PARAM) {
            _emitf(out, sizeof(out),
                   "params: %s is not a parameter (overrides set %s <value> to force it)",
                   p.meta()->name, p.meta()->name);
            return;
        }

        char val[96];
        uint16_t vc = std::min(wv, (uint16_t)(sizeof(val) - 1));
        memcpy(val, v, vc); val[vc] = '\0';

        if (io->set(p, val) != Vars::SET_OK) {
            _emitf(out, sizeof(out), "params: invalid value for %s", p.meta()->name);
            return;
        }
        _emitf(out, sizeof(out), "%s = %s  (unsaved: `params save %s`)",
               p.meta()->name, val, p.meta()->name);
    }

    void CommandShell::_params_save(const char* nm, uint16_t wn) {
        Vars::Bank* io = _bank();
        char out[128];
        if (io == nullptr) { _println("params: no variables"); return; }

        if (nm == nullptr) { _println("usage: params save <name>|all"); return; }

        if (arg_is_all(nm, wn)) {
            uint16_t refused = 0;
            const uint16_t saved = io->save_params(&refused);
            if (saved == 0 && refused == 0) { _println("params: nothing to save"); return; }
            if (refused != 0)
                _emitf(out, sizeof(out), "params: %u saved, %u REFUSED by storage",
                       saved, refused);
            else
                _emitf(out, sizeof(out), "params: %u saved", saved);
            return;
        }

        const Vars::VarRef r = io->find(nm, wn);
        if (r.index == Vars::VAR_NONE) { _println("params: unknown parameter"); return; }
        Vars::Var p = io->var(r);
        if (p.role() != Vars::ROLE_PARAM) {
            _emitf(out, sizeof(out), "params: %s is not a parameter", p.meta()->name);
            return;
        }
        switch (io->save(p)) {
            case Vars::SET_OK:
                _emitf(out, sizeof(out), "%s saved", p.meta()->name);
                break;
            case Vars::SET_NOT_PERSISTED:
                // The value stays live and stays marked, so the next `params
                // save` tries again: say which of the two failed, the fixes are
                // different.
                _emitf(out, sizeof(out), "%s NOT saved: storage refused it",
                       p.meta()->name);
                break;
            default:
                _emitf(out, sizeof(out), "params: cannot save %s", p.meta()->name);
                break;
        }
    }

    // Shared by every table verb: one lookup, one message.
    Vars::VarRef CommandShell::_resolve(const char* noun, const char* nm,
                                        uint16_t wn) {
        Vars::Bank* io = _bank();
        char err[96];
        const Vars::VarRef none = { Vars::VAR_NONE };
        if (io == nullptr) { _emitf(err, sizeof(err), "%s: no variables", noun); return none; }
        const Vars::VarRef r = io->find(nm, wn);
        if (r.index == Vars::VAR_NONE) _emitf(err, sizeof(err), "%s: unknown variable", noun);
        return r;
    }

    void CommandShell::_cmd_overrides(const char* args, uint16_t alen) {
        if (_bank() == nullptr) { _println("overrides: no variables"); return; }

        uint16_t wv; const char* verb = arg_next(args, alen, wv);
        if (arg_eq(verb, wv, "list")) { _overrides_list(); return; }

        uint16_t wn; const char* nm = arg_next(args, alen, wn);
        if (arg_eq(verb, wv, "get"))   { _show_var("overrides", nm, wn, false); return; }
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
            if (!h.overridden()) continue;
            if (h.override_unsaved()) unsaved++;
            _emitf(out, sizeof(out), "  %-20s %-12s %s%s", h.meta()->name,
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
        Vars::Bank* io = _bank();
        char out[128];
        if (nm == nullptr) { _println("usage: overrides clear <name>|all"); return; }

        if (arg_is_all(nm, wn)) {
            _emitf(out, sizeof(out), "overrides: cleared %u", io->clear_overrides());
            return;
        }
        const Vars::VarRef r = _resolve("overrides", nm, wn);
        if (r.index == Vars::VAR_NONE) return;
        Vars::Var h = io->var(r);
        // Through the bank, not the handle: the handle cannot reach the store,
        // and a release leaving the stored copy behind would come back at the
        // next boot.
        io->override_clear(h);
        _emitf(out, sizeof(out), "%s: override cleared", h.meta()->name);
    }

    void CommandShell::_overrides_set(const char* nm, uint16_t wn,
                                      const char* args, uint16_t alen) {
        Vars::Bank* io = _bank();
        char out[128];
        if (nm == nullptr) {
            _println("usage: overrides set <name> [<value>] [valid|invalid]");
            return;
        }
        const Vars::VarRef r = _resolve("overrides", nm, wn);
        if (r.index == Vars::VAR_NONE) return;
        Vars::Var h = io->var(r);

        uint16_t wa; const char* a = arg_next(args, alen, wa);

        // `set <name> off` is the older spelling for a release, and it reads
        // naturally next to a value; `on` is optional sugar before one.
        if (a != nullptr && arg_eq(a, wa, "off")) {
            io->override_clear(h);
            _emitf(out, sizeof(out), "%s: override cleared", h.meta()->name);
            return;
        }
        if (a != nullptr && arg_eq(a, wa, "on")) a = arg_next(args, alen, wa);

        // A trailing `valid` / `invalid` says what the forced value READS as:
        // "invalid" stands a variable up as a dead sensor without touching what
        // it reports. Accepted either side of the value.
        bool valid = true;
        if (arg_validity(a, wa, valid)) a = arg_next(args, alen, wa);

        if (a == nullptr) {
            // No value: hold it where it is, with the quality asked for rather
            // than the one it happens to have.
            io->override_set(h, h.raw(), valid);
            _override_reply(h, "frozen at");
            return;
        }

        char text[64];
        const uint16_t vc = std::min(wa, (uint16_t)(sizeof(text) - 1));
        memcpy(text, a, vc); text[vc] = '\0';

        // Sequenced deliberately: arg_next() WRITES wt, so passing both to one
        // call leaves the read of wt unordered against it.
        uint16_t wt = 0;
        const char* t = arg_next(args, alen, wt);
        (void)arg_validity(t, wt, valid);

        if (io->override_set(h, text, valid) != Vars::SET_OK) {
            _emitf(out, sizeof(out), "overrides: invalid value for %s", h.meta()->name);
            return;
        }
        _override_reply(h, "forced to");
    }

    void CommandShell::_override_reply(Vars::Var h, const char* verb) {
        Vars::Bank* io = _bank();
        char out[128], val[48];
        _emitf(out, sizeof(out), "%s: %s %s%s  (unsaved: `overrides save %s`)",
               h.meta()->name, verb, Vars::var_format(*io, h, val, sizeof(val)),
               h.valid() ? "" : " (reading as invalid)", h.meta()->name);
    }

    // An override is live the moment it is taken and survives a reboot only
    // once saved. That is deliberate: forcing a signal to chase a fault is the
    // common case, and it should not outlive the session by default - while an
    // override left in place ON PURPOSE (a failed sensor bypassed until the
    // spare arrives) is exactly what wants committing.
    void CommandShell::_overrides_save(const char* nm, uint16_t wn) {
        Vars::Bank* io = _bank();
        char out[128];
        if (io == nullptr) { _println("overrides: no variables"); return; }

        if (nm == nullptr) { _println("usage: overrides save <name>|all"); return; }

        if (arg_is_all(nm, wn)) {
            uint16_t refused = 0;
            const uint16_t saved = io->save_overrides(&refused);
            if (saved == 0 && refused == 0) { _println("overrides: nothing to save"); return; }
            if (refused != 0)
                _emitf(out, sizeof(out), "overrides: %u saved, %u REFUSED by storage",
                       saved, refused);
            else
                _emitf(out, sizeof(out), "overrides: %u saved", saved);
            return;
        }

        const Vars::VarRef r = io->find(nm, wn);
        if (r.index == Vars::VAR_NONE) { _println("overrides: unknown variable"); return; }
        Vars::Var h = io->var(r);
        switch (io->save_override(h)) {
            case Vars::SET_OK:
                _emitf(out, sizeof(out), "%s: override saved", h.meta()->name);
                break;
            case Vars::SET_NOT_PERSISTED:
                _emitf(out, sizeof(out), "%s: override NOT saved: storage refused it",
                       h.meta()->name);
                break;
            default:
                _emitf(out, sizeof(out), "%s: nothing forced on it", h.meta()->name);
                break;
        }
    }

}}  // namespace Botanik::Shell
