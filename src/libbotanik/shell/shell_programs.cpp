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

#include <cstring>

#include "libbotanik/hal/platform.h"
#include "libbotanik/plc/program.h"
#include "libbotanik/tasks/tasks.h"

namespace Botanik { namespace Shell {

#if BOTANIK_FEATURE_PLC

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
                if (nm != nullptr && strncmp(p->name(), nm, wn) != 0) continue;
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

        if (arg_eq(verb, wv, "enable"))  { _programs_gate(nm, wn, true);  return; }
        if (arg_eq(verb, wv, "disable")) { _programs_gate(nm, wn, false); return; }
        if (arg_eq(verb, wv, "save"))    { _programs_save(nm, wn);        return; }

        _verb_usage("programs");
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

#endif

}}  // namespace Botanik::Shell
