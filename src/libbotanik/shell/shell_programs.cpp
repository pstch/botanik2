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
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_SHELL_PAGE && BOTANIK_FEATURE_PLC
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
                const Plc::Mode m = reg->program_mode(i);
                if (m != Plc::Mode::NORMAL) off++;
                if (reg->program_unsaved(i)) unsaved++;
                // The COMMANDED rung beside the EFFECTIVE verdict. Both,
                // because the plant's own ask is weighed with the operator's -
                // one column would leave "it says safe, why is it still
                // moving" unanswerable.
                _emitf(out, sizeof(out), "  %-20s %-9s %-9s%s", p->name(),
                       Plc::mode_name(m), Plc::health_name(p->health()),
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
        // The verb IS the rung. `release` is an alias for `enable`, which reads
        // better against the three that take something away.
        if (arg_eq(verb, wv, "enable") || arg_eq(verb, wv, "release"))
            { _programs_mode(nm, wn, Plc::Mode::NORMAL);    return; }
        if (arg_eq(verb, wv, "inhibit"))
            { _programs_mode(nm, wn, Plc::Mode::INHIBITED); return; }
        if (arg_eq(verb, wv, "safe"))
            { _programs_mode(nm, wn, Plc::Mode::SAFE);      return; }
        if (arg_eq(verb, wv, "disable"))
            { _programs_mode(nm, wn, Plc::Mode::DISABLED);  return; }
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
        // SAFE is named too: a program can reach it UNASKED, through its own
        // go_safe(), so a tally alone would leave an operator with no way to
        // find which law took itself out.
        static const Plc::Health NAMED[] = { Plc::Health::INVALID,
                                             Plc::Health::DEGRADED,
                                             Plc::Health::PARKING,
                                             Plc::Health::SAFE };
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

    void CommandShell::_programs_mode(const char* nm, uint16_t wn, Plc::Mode m) {
        Tasks::Registry* reg = _registry();
        char out[128];
        if (reg == nullptr) { _println("programs: no task table"); return; }
        const uint64_t now = HAL::Clock::uptime_ms();

        if (nm == nullptr) { _verb_usage("programs", Plc::mode_name(m)); return; }

        if (arg_is_all(nm, wn)) {
            const uint8_t n = reg->program_count();
            uint8_t moved = 0;
            for (uint8_t i = 0; i < n; i++) {
                if (reg->program_mode(i) == m) continue;
                if (reg->set_program_mode(i, m, now)) moved++;
            }
            _emitf(out, sizeof(out), "programs: %u %s%s", moved, Plc::mode_name(m),
                   moved == 0 ? "" : "  (unsaved: `programs save all`)");
            return;
        }

        const int16_t i = reg->find_program(nm, wn);
        if (i < 0) { _println("programs: unknown program"); return; }
        reg->set_program_mode((uint8_t)i, m, now);
        const Plc::Program* p = reg->program((uint8_t)i);
        // What was ASKED and where it is NOW, because parking is work: a rung
        // is reached over several beats and the first answer is PARKING.
        _emitf(out, sizeof(out), "%s: %s, now %s  (unsaved: `programs save %s`)",
               p->name(), Plc::mode_name(m), Plc::health_name(p->health()),
               p->name());
    }

    void CommandShell::_programs_save(const char* nm, uint16_t wn) {
        Tasks::Registry* reg = _registry();
        char out[128];
        if (reg == nullptr) { _println("programs: no task table"); return; }

        if (nm == nullptr) { _verb_usage("programs", "save"); return; }

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
    void CommandShell::_programs_mode(const char*, uint16_t, Plc::Mode) {}
    void CommandShell::_programs_save(const char*, uint16_t) {}
    void CommandShell::_programs_status() {}

#endif

}}  // namespace Botanik::Shell

#endif // BOTANIK_FEATURE_SHELL_PAGE && BOTANIK_FEATURE_PLC
