/**
 * @file shell_alarms.cpp
 * @brief `alarms`: the condition table, one condition, and acknowledging.
 */
#include "libbotanik/utils/minmax.h"
#include "libbotanik/shell/shell.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_SHELL_PAGE
#include "libbotanik/shell/args.h"

#include <cstdio>
#include <cstring>

#include "libbotanik/alarms/alarms.h"
#include "libbotanik/utils/text.h"

namespace Botanik { namespace Shell {

    void CommandShell::_cmd_alarms(const char* args, uint16_t alen) {
        uint8_t n; const Verb* V = _alarms_verbs(n);
        if (!_run_verb(V, n, args, alen)) _verb_usage("alarms");
    }

    // "alarms ack|unack <code|all>" - mutate, so they need the model wired via
    // set_diagnostics. Both spellings share a body: they are one gesture, and
    // an ack put on the wrong row is a mis-keystroke to undo.
    void CommandShell::_alarms_ack(const char* args, uint16_t alen) { _ack(args, alen, true); }
    void CommandShell::_alarms_unack(const char* args, uint16_t alen) { _ack(args, alen, false); }

    void CommandShell::_ack(const char* args, uint16_t alen, bool ack) {
        char out[80];
        if (_alarm_model == nullptr) { _println("alarms: model unavailable"); return; }
        uint16_t tlen; const char* tgt = arg_next(args, alen, tlen);
        if (tgt == nullptr) { _verb_usage("alarms", ack ? "ack" : "unack"); return; }
        if (arg_eq(tgt, tlen, "all")) {
            _emitf(out, sizeof(out),
                   ack ? "acknowledged %u active alarm(s)"
                       : "took back %u acknowledgement(s)",
                   ack ? _alarm_model->ack_all() : _alarm_model->unack_all());
            return;
        }
        char code[24];
        uint16_t cl = Utils::min(tlen, (uint16_t)(sizeof(code) - 1));
        memcpy(code, tgt, cl); code[cl] = '\0';
        const bool hit = ack ? _alarm_model->ack(code) : _alarm_model->unack(code);
        _emitf(out, sizeof(out),
               ack ? (hit ? "acknowledged %s" : "no active alarm '%s'")
                   : (hit ? "%s: acknowledgement taken back"
                          : "no acknowledged alarm '%s'"),
               code);
    }

    // "alarms list [<prefix>]" - one paced line per active condition: code,
    // name, age, flags. The prefix filters on the CODE, which is what the alarm
    // bar shows and how a plant's alarms are grouped (`TOO_`, `WTR_`).
    void CommandShell::_alarms_list(const char* args, uint16_t alen) {
        const Alarms::AlarmController* m = _alarm_model;
        if (m == nullptr) { _println("alarms: model unavailable"); return; }
        char out[80], age[16];
        uint16_t wp = 0;
        const char* prefix = arg_next(args, alen, wp);
        const uint64_t now = HAL::Clock::uptime_ms();
        uint16_t n = 0;
        for (uint8_t i = 0; i < m->count(); i++) {
            const Alarms::AlarmCondition& c = m->condition(i);
            if (!c.active) continue;
            const char* code = c.code() ? c.code() : "?";
            if (!Utils::glob_bounded(code, prefix, wp)) continue;
            uint32_t secs = (uint32_t)((now - c.started_ms) / 1000);
            // Code first (what the alarm bar shows and what `alarms ack`
            // takes), then the variable name (what `vars get` takes).
            _emitf(out, sizeof(out), "  %-9s %-22s %s%s",
                code,
                c.name() ? c.name() : "?",
                Utils::format_age(age, sizeof(age), secs, Utils::AgeSuffix::BARE),
                c.user_ack ? " [ack]" : "");
            n++;
        }
        if (n == 0) _println("no active alarms");
    }

    // "alarms get <code|name>" - the condition behind one alarm: what it is
    // doing now, and the row that raises it.
    void CommandShell::_alarms_get(const char* args, uint16_t alen) {
        const Alarms::AlarmController* m = _alarm_model;
        if (m == nullptr) { _println("alarms: model unavailable"); return; }
        char out[80];
        uint16_t wt = 0;
        const char* tgt = arg_next(args, alen, wt);
        if (tgt == nullptr) { _verb_usage("alarms", "get"); return; }
        char key[24];
        uint16_t kl = Utils::min(wt, (uint16_t)(sizeof(key) - 1));
        memcpy(key, tgt, kl); key[kl] = '\0';
        const int idx = m->index_of(key);
        if (idx < 0) { _emitf(out, sizeof(out), "alarms: no alarm '%s'", key); return; }
        const Alarms::AlarmCondition& c = m->condition((uint8_t)idx);

        char age[16], det[64];
        const uint64_t now = HAL::Clock::uptime_ms();
        _emitf(out, sizeof(out), "%s = %s", c.code() ? c.code() : "?",
               c.active ? "ACTIVE" : "clear");
        _field("Variable", c.name() ? c.name() : "?", nullptr);
        if (c.active) {
            uint32_t secs = (uint32_t)((now - c.started_ms) / 1000);
            _field("Active for",
                   Utils::format_age(age, sizeof(age), secs, Utils::AgeSuffix::BARE),
                   nullptr);
        }
        _field("Acknowledged", c.user_ack ? "yes" : "no", nullptr);
        // The policy, since an alarm that "should" be up and is not is usually
        // inside its on-delay, and one that should be down is usually inside
        // its hold.
        snprintf(det, sizeof(det), "%ds on, %ds off, %ds hold",
                 (int)c.on_s(), (int)c.off_s(), (int)c.hold_s());
        _field("Dwell", det, nullptr);
        if (c.desc()  && c.desc()[0])  _field("Description", c.desc(),  nullptr);
        if (c.notes() && c.notes()[0]) _field("Remediation", c.notes(), nullptr);
    }

    /** @brief `alarms`'s verbs. @see CommandShell::Verb */
    const CommandShell::Verb* CommandShell::_alarms_verbs(uint8_t& count) {
        static const Verb V[] = {
            { "list [<pat>]         code, or a glob: TOO_*",  &CommandShell::_alarms_list  },
            { "get <code|name>",                              &CommandShell::_alarms_get   },
            { "ack <code|name|all>",                          &CommandShell::_alarms_ack   },
            { "unack <code|name|all>",                        &CommandShell::_alarms_unack },
        };
        count = (uint8_t)(sizeof(V) / sizeof(V[0]));
        return V;
    }

}}  // namespace Botanik::Shell

#endif // BOTANIK_FEATURE_SHELL_PAGE
