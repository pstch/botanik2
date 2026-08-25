/**
 * @file shell_alarms.cpp
 * @brief `alarms`: the condition table, one condition, and acknowledging.
 */
#include "libbotanik/shell/shell.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_SHELL_PAGE
#include "libbotanik/shell/args.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "libbotanik/alarms/alarms.h"
#include "libbotanik/utils/text.h"

namespace Botanik { namespace Shell {

    void CommandShell::_cmd_alarms(const char* args, uint16_t alen) {
        uint16_t wlen; const char* sub = arg_next(args, alen, wlen);
        char out[80];

        // "alarms ack <code|all>" - mutates, so it needs the model wired via
        // set_diagnostics.
        if (sub && arg_eq(sub, wlen, "ack")) {
            if (_alarm_model == nullptr) {
                _println("alarms: model unavailable"); return;
            }
            uint16_t tlen; const char* tgt = arg_next(args, alen, tlen);
            if (tgt == nullptr) {
                _println("usage: alarms ack <code|name|all>"); return;
            }
            if (arg_eq(tgt, tlen, "all")) {
                _emitf(out, sizeof(out), "acknowledged %u active alarm(s)",
                       _alarm_model->ack_all());
            } else {
                char code[24];
                uint16_t cl = std::min(tlen, (uint16_t)(sizeof(code) - 1));
                memcpy(code, tgt, cl); code[cl] = '\0';
                _emitf(out, sizeof(out), _alarm_model->ack(code)
                       ? "acknowledged %s" : "no active alarm '%s'", code);
            }
            return;
        }

        const Alarms::AlarmController* m = _alarm_model;
        if (m == nullptr) { _println("alarms: model unavailable"); return; }

        // "alarms list [<prefix>]" - one paced line per active condition: code,
        // name, age, flags. The prefix filters on the CODE, which is what the
        // alarm bar shows and how a plant's alarms are grouped (`TOO_`, `WTR_`).
        if (sub && arg_eq(sub, wlen, "list")) {
            uint16_t wp = 0;
            const char* prefix = arg_next(args, alen, wp);
            char age[16];
            uint64_t now = HAL::Clock::uptime_ms();
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
                    Utils::format_age(age, sizeof(age), secs, false),
                    c.user_ack ? " [ack]" : "");
                n++;
            }
            if (n == 0) _println("no active alarms");
            return;
        }

        // "alarms get <code|name>" - the condition behind one alarm: what it is
        // doing now, and the row that raises it.
        if (sub && arg_eq(sub, wlen, "get")) {
            uint16_t wt = 0;
            const char* tgt = arg_next(args, alen, wt);
            if (tgt == nullptr) { _println("usage: alarms get <code|name>"); return; }
            char key[24];
            uint16_t kl = std::min(wt, (uint16_t)(sizeof(key) - 1));
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
                _field("Active for", Utils::format_age(age, sizeof(age), secs, false), nullptr);
            }
            _field("Acknowledged", c.user_ack ? "yes" : "no", nullptr);
            // The policy, since an alarm that "should" be up and is not is
            // usually inside its on-delay, and one that should be down is
            // usually inside its hold.
            snprintf(det, sizeof(det), "%ds on, %ds off, %ds hold",
                     (int)c.on_s(), (int)c.off_s(), (int)c.hold_s());
            _field("Dwell", det, nullptr);
            if (c.desc()  && c.desc()[0])  _field("Description", c.desc(),  nullptr);
            if (c.notes() && c.notes()[0]) _field("Remediation", c.notes(), nullptr);
            return;
        }

        _verb_usage("alarms");
    }

}}  // namespace Botanik::Shell

#endif // BOTANIK_FEATURE_SHELL_PAGE
