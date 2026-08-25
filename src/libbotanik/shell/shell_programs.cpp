/**
 * @file shell_programs.cpp
 * @brief `programs`: which control programs are registered, and taking one out
 *        of the scan without rebuilding.
 *
 * A program's gate is the operator's, beside the system's control authority,
 * and the stricter wins - so `programs enable` on a unit whose control is
 * inhibited changes nothing visible, which is what `control` is for reporting.
 */
#include "libbotanik/shell/shell.h"
#include "libbotanik/shell/args.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "libbotanik/hal/platform.h"
#include "libbotanik/plc/program.h"
#include "libbotanik/tasks/tasks.h"
#include "libbotanik/utils/text.h"

namespace Botanik { namespace Shell {

#if BOTANIK_FEATURE_PLC

    /** @brief How many names `programs status` prints per state before it
     *  falls back to a count. A state with more members than this is one the
     *  operator reads as a number anyway. */
    static const uint8_t NAME_CAP = 4;

    /** @brief Append to @p buf at @p at, leaving @p at inside cap-1 whatever
     *  printf says it WOULD have written - so a truncated line stops growing
     *  rather than indexing past the end. */
    static void _append(char* buf, size_t cap, uint16_t& at, const char* fmt, ...)
        __attribute__((format(printf, 4, 5)));

    static void _append(char* buf, size_t cap, uint16_t& at, const char* fmt, ...) {
        if ((size_t)at + 1 >= cap) return;
        va_list ap; va_start(ap, fmt);
        const int w = vsnprintf(buf + at, cap - at, fmt, ap);
        va_end(ap);
        if (w < 0) return;
        at = (at + (size_t)w >= cap) ? (uint16_t)(cap - 1) : (uint16_t)(at + w);
    }

    void CommandShell::_cmd_programs(const char* args, uint16_t alen) {
        Tasks::Registry* reg = _registry();
        char out[128];
        if (reg == nullptr) { _println("programs: no task table"); return; }

        uint16_t wv; const char* verb = arg_next(args, alen, wv);
        uint16_t wn; const char* nm   = arg_next(args, alen, wn);

        if (arg_eq(verb, wv, "list")) {
            const uint8_t n = reg->program_count();
            uint8_t shown = 0, off = 0, unsaved = 0;
            for (uint8_t i = 0; i < n; i++) {
                const Plc::Program* p = reg->program(i);
                if (p == nullptr) continue;
                if (!Utils::glob_bounded(p->name(), nm, wn)) continue;
                const bool on = reg->program_enabled(i);
                if (!on) off++;
                if (reg->program_unsaved(i)) unsaved++;
                _emitf(out, sizeof(out), "  %-20s %-8s%s", p->name(),
                       on ? "enabled" : "DISABLED",
                       reg->program_unsaved(i) ? " (unsaved)" : "");
                shown++;
            }
            if (shown == 0) { _println("programs: none"); return; }
            // The scan is gated by control authority too, and a program that
            // reads "enabled" while control is withheld is still not running.
            _emitf(out, sizeof(out), "%u program(s), %u disabled, %u unsaved%s",
                   shown, off, unsaved,
                   reg->programs_enabled() ? "" : "  (control withheld: none are running)");
            return;
        }

        if (arg_eq(verb, wv, "status"))  { _programs_status();            return; }
        if (arg_eq(verb, wv, "enable"))  { _programs_gate(nm, wn, true);  return; }
        if (arg_eq(verb, wv, "disable")) { _programs_gate(nm, wn, false); return; }
        if (arg_eq(verb, wv, "save"))    { _programs_save(nm, wn);        return; }

        _verb_usage("programs");
    }

    void CommandShell::_programs_status() {
        Tasks::Registry* reg = _registry();
        char out[160];
        if (reg == nullptr) { _println("programs: no task table"); return; }

        const uint8_t n = reg->program_count();
        uint8_t tally[Plc::HEALTH_COUNT] = { 0 };
        uint8_t total = 0;
        for (uint8_t i = 0; i < n; i++) {
            const Plc::Program* p = reg->program(i);
            if (p == nullptr) continue;
            const uint8_t h = (uint8_t)p->health();
            if (h < Plc::HEALTH_COUNT) tally[h]++;
            total++;
        }
        if (total == 0) { _println("programs: none"); return; }

        // The whole tally on one line, in enum order, states with nobody in
        // them left out.
        uint16_t at = 0;
        _append(out, sizeof(out), at, "programs: %u  (", (unsigned)total);
        bool first = true;
        for (uint8_t h = 0; h < Plc::HEALTH_COUNT; h++) {
            if (tally[h] == 0) continue;
            _append(out, sizeof(out), at, "%s%u %s", first ? "" : ", ",
                    (unsigned)tally[h], Plc::health_name((Plc::Health)h));
            first = false;
        }
        _append(out, sizeof(out), at, ")");
        _println(out);

        // Named, worst first. HEALTHY and INHIBITED are counted and not listed:
        // with control withheld or before the first scan EVERY program is
        // inhibited, and a screen of names says less than the number does.
        static const Plc::Health NAMED[] = { Plc::Health::FAULTED,
                                             Plc::Health::DEGRADED,
                                             Plc::Health::PARKING };
        for (Plc::Health h : NAMED) {
            const uint8_t code = (uint8_t)h;
            if (tally[code] == 0) continue;
            at = 0;
            _append(out, sizeof(out), at, "  %-9s ", Plc::health_name(h));
            uint8_t shown = 0;
            for (uint8_t i = 0; i < n && shown < NAME_CAP; i++) {
                const Plc::Program* p = reg->program(i);
                if (p == nullptr || p->health() != h) continue;
                _append(out, sizeof(out), at, "%s%s", shown == 0 ? "" : ", ",
                        p->name());
                shown++;
            }
            if (tally[code] > shown)
                _append(out, sizeof(out), at, " +%u more",
                        (unsigned)(tally[code] - shown));
            _println(out);
        }
    }

    void CommandShell::_programs_gate(const char* nm, uint16_t wn, bool on) {
        Tasks::Registry* reg = _registry();
        char out[128];
        if (reg == nullptr) { _println("programs: no task table"); return; }
        const uint64_t now = HAL::Clock::uptime_ms();

        if (nm == nullptr) { _println("usage: programs enable|disable <name>|all"); return; }

        if (arg_is_all(nm, wn)) {
            const uint8_t n = reg->program_count();
            uint8_t moved = 0;
            for (uint8_t i = 0; i < n; i++) {
                if (reg->program_enabled(i) == on) continue;
                if (reg->set_program_enabled(i, on, now)) moved++;
            }
            _emitf(out, sizeof(out), "programs: %u %s%s", moved,
                   on ? "enabled" : "disabled",
                   moved == 0 ? "" : "  (unsaved: `programs save all`)");
            return;
        }

        const int16_t i = reg->find_program(nm, wn);
        if (i < 0) { _println("programs: unknown program"); return; }
        reg->set_program_enabled((uint8_t)i, on, now);
        _emitf(out, sizeof(out), "%s: %s  (unsaved: `programs save %s`)",
               reg->program((uint8_t)i)->name(), on ? "enabled" : "disabled",
               reg->program((uint8_t)i)->name());
    }

    void CommandShell::_programs_save(const char* nm, uint16_t wn) {
        Tasks::Registry* reg = _registry();
        char out[128];
        if (reg == nullptr) { _println("programs: no task table"); return; }

        if (nm == nullptr) { _println("usage: programs save <name>|all"); return; }

        if (arg_is_all(nm, wn)) {
            uint8_t refused = 0;
            const uint8_t saved = reg->save_programs(&refused);
            if (saved == 0 && refused == 0) { _println("programs: nothing to save"); return; }
            if (refused != 0)
                _emitf(out, sizeof(out), "programs: %u saved, %u REFUSED by storage",
                       saved, refused);
            else
                _emitf(out, sizeof(out), "programs: %u saved", saved);
            return;
        }

        const int16_t i = reg->find_program(nm, wn);
        if (i < 0) { _println("programs: unknown program"); return; }
        const char* name = reg->program((uint8_t)i)->name();
        if (reg->save_program((uint8_t)i))
            _emitf(out, sizeof(out), "%s saved", name);
        else
            _emitf(out, sizeof(out), "%s NOT saved: storage refused it", name);
    }

#else   // !BOTANIK_FEATURE_PLC

    void CommandShell::_cmd_programs(const char*, uint16_t) {
        _println("programs: this build has no scan cycle");
    }
    void CommandShell::_programs_gate(const char*, uint16_t, bool) {}
    void CommandShell::_programs_save(const char*, uint16_t) {}
    void CommandShell::_programs_status() {}

#endif

}}  // namespace Botanik::Shell
