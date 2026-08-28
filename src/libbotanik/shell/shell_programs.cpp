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

    /** @brief Append to @p buf at @p at, leaving @p at inside cap-1 whatever
     *  printf says it WOULD have written - so a truncated line stops growing
     *  rather than indexing past the end. */
    void CommandShell::_cmd_programs(const char* args, uint16_t alen) {
        Tasks::Registry* reg = _registry();
        char out[128];
        if (reg == nullptr) { _println("programs: no task table"); return; }

        uint16_t wv; const char* verb = arg_next(args, alen, wv);
        uint16_t wn; const char* nm   = arg_next(args, alen, wn);

        if (arg_eq(verb, wv, "list")) {
            const uint8_t n = reg->program_count();
            uint8_t shown = 0, bad = 0;
            for (uint8_t i = 0; i < n; i++) {
                const Plc::Program* p = reg->program(i);
                if (p == nullptr) continue;
                if (!Utils::glob_bounded(p->name(), nm, wn)) continue;
                if (p->is_invalid()) bad++;
                _emitf(out, sizeof(out), "  %-20s %s", p->name(),
                       Plc::health_name(p->health()));
                shown++;
            }
            if (shown == 0) { _println("programs: none"); return; }
            _emitf(out, sizeof(out), "%u program(s), %u INVALID", shown, bad);
            return;
        }

        if (arg_eq(verb, wv, "status"))  { _programs_status();            return; }

        _verb_usage("programs");
    }

    void CommandShell::_programs_status() {
        Tasks::Registry* reg = _registry();
        char out[160];
        if (reg == nullptr) { _println("programs: no task table"); return; }

        const uint8_t n = reg->program_count();
        uint8_t total = 0, bad = 0;
        for (uint8_t i = 0; i < n; i++) {
            const Plc::Program* p = reg->program(i);
            if (p == nullptr) continue;
            if (p->is_invalid()) bad++;
            total++;
        }
        if (total == 0) { _println("programs: none"); return; }
        _emitf(out, sizeof(out), "programs: %u  (%u INVALID)",
               (unsigned)total, (unsigned)bad);

        // Named, because there is nothing else to say about one: a program
        // that could not be wired never runs, and which one it is is the whole
        // of what an operator needs.
        for (uint8_t i = 0; i < n && bad != 0; i++) {
            const Plc::Program* p = reg->program(i);
            if (p == nullptr || !p->is_invalid()) continue;
            _emitf(out, sizeof(out), "  INVALID   %s", p->name());
        }
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
