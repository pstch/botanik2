/**
 * @file shell_programs.cpp
 * @brief `programs`: which control programs are registered, and taking one out
 *        of the scan without rebuilding.
 *
 * A program's gate is the operator's, beside the system's control authority,
 * and the stricter wins - so `programs start` on a unit whose control is
 * inhibited changes nothing visible, which is why `programs status` reports
 * sys_control beside the counts.
 *
 * start/stop are this run's; enable/disable are the same edit made durable.
 * The gate IS a parameter, `prog_<name>_run`, so a law left out of the scan is
 * a row an operator can read on the PARAMS page rather than a bit in RAM.
 */
#include "libbotanik/plc/health.h"
#include "libbotanik/shell/shell.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_SHELL_PAGE && BOTANIK_FEATURE_PLC
#include "libbotanik/shell/args.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "libbotanik/hal/platform.h"
#include "libbotanik/core/context.h"
#include "libbotanik/plc/runtime/runtime.h"
#include "libbotanik/tasks/tasks.h"
#include "libbotanik/utils/text.h"
#include "libbotanik/vars/persist.h"

namespace Botanik { namespace Shell {

    /** @brief Append to @p buf at @p at, leaving @p at inside cap-1 whatever
     *  printf says it WOULD have written - so a truncated line stops growing
     *  rather than indexing past the end. */
    void CommandShell::_cmd_programs(const char* args, uint16_t alen) {
        uint8_t n; const Verb* V = _programs_verbs(n);
        if (!_run_verb(V, n, args, alen)) _verb_usage("programs");
    }

    void CommandShell::_programs_list(const char* args, uint16_t alen) {
        Plc::Runtime::Plant* pl = (_sys != nullptr) ? _sys->plant : nullptr;
        char out[128];
        if (pl == nullptr) { _println("programs: no plant"); return; }

        uint16_t wn; const char* nm = arg_next(args, alen, wn);
        const uint8_t n = pl->count();
        uint8_t shown = 0;
        for (uint8_t i = 0; i < n; i++) {
            if (!Utils::glob_bounded(pl->name(i), nm, wn)) continue;
            _emitf(out, sizeof(out), "  %-22s %-8s %s", pl->name(i),
                   Plc::health_name(pl->health(i)),
                   pl->running(i)   ? "running"
                   : !pl->enabled(i) ? "STOPPED"
                                     : "held");
            shown++;
        }
        if (shown == 0) { _println("programs: none"); return; }
        _emitf(out, sizeof(out), "%u program(s)", shown);
    }

    // start/stop are this run; enable/disable are the same write plus the save.
    void CommandShell::_programs_start(const char* a, uint16_t n)   { _gate_verb(a, n, true,  false); }
    void CommandShell::_programs_stop(const char* a, uint16_t n)    { _gate_verb(a, n, false, false); }
    void CommandShell::_programs_enable(const char* a, uint16_t n)  { _gate_verb(a, n, true,  true);  }
    void CommandShell::_programs_disable(const char* a, uint16_t n) { _gate_verb(a, n, false, true);  }
    void CommandShell::_programs_show(const char*, uint16_t)        { _programs_status(); }

    void CommandShell::_gate_verb(const char* args, uint16_t alen, bool on, bool persist) {
        uint16_t wn; const char* nm = arg_next(args, alen, wn);
        _programs_gate(nm, wn, on, persist);
    }

    /** @brief Put the programs @p nm globs in or out of the scan. A stopped
     *  one drops what it drives to its declared safe state on the next beat,
     *  which is the whole point of the verb - a coil left energised by a law
     *  nobody is running is what this exists to prevent. */
    void CommandShell::_programs_gate(const char* nm, uint16_t wn, bool on,
                                      bool persist) {
        Plc::Runtime::Plant* pl = (_sys != nullptr) ? _sys->plant : nullptr;
        char out[128];
        if (pl == nullptr) { _println("programs: no plant"); return; }
        if (nm == nullptr || wn == 0) {
            _println("programs: name it, or `all`");
            return;
        }
        Vars::Persist* st = persist ? _store() : nullptr;
        const bool all = arg_eq(nm, wn, "all");
        uint8_t hit = 0, kept = 0;
        for (uint8_t i = 0; i < pl->count(); i++) {
            if (!all && !Utils::glob_bounded(pl->name(i), nm, wn)) continue;
            pl->set_enabled(i, on);
            hit++;
            if (st != nullptr && st->save(pl->gate(i)) == Vars::SET_OK) kept++;
        }
        if (hit == 0) { _println("programs: no such program"); return; }
        const char* what = on ? (persist ? "ENABLED" : "started")
                              : (persist ? "DISABLED" : "STOPPED");
        if (!persist)
            _emitf(out, sizeof(out), "%u program(s) %s  (this run only)", hit, what);
        else if (st == nullptr)
            _emitf(out, sizeof(out), "%u program(s) %s, but there is nowhere "
                   "to save it", hit, what);
        else if (kept == hit)
            _emitf(out, sizeof(out), "%u program(s) %s and saved", hit, what);
        else
            _emitf(out, sizeof(out), "%u program(s) %s, but storage REFUSED "
                   "%u of the saves", hit, what, (unsigned)(hit - kept));
        // The operator's gate is one of two, and saying so here is cheaper than
        // an operator wondering why an enabled program is not running.
        if (on && !pl->control_allowed())
            _println("  ...but sys_control is off: none of them will run");
    }

    void CommandShell::_programs_status() {
        Plc::Runtime::Plant* pl = (_sys != nullptr) ? _sys->plant : nullptr;
        char out[160];
        if (pl == nullptr) { _println("programs: no plant"); return; }

        const uint8_t n = pl->count();
        uint8_t bad = 0, off = 0, run = 0;
        for (uint8_t i = 0; i < n; i++) {
            if (pl->health(i) != Plc::Health::HEALTHY) bad++;
            else if (!pl->enabled(i))                  off++;
            if (pl->running(i))                        run++;
        }
        _emitf(out, sizeof(out), "programs: %u  (%u running, %u STOPPED, %u INVALID)",
               (unsigned)n, (unsigned)run, (unsigned)off, (unsigned)bad);
        _emitf(out, sizeof(out), "control: %s",
               pl->control_allowed() ? "sys_control on" : "sys_control OFF");

        // Named, because there is nothing else to say about one: a program
        // that could not be wired never runs, and which one it is is the whole
        // of what an operator needs.
        for (uint8_t i = 0; i < n; i++) {
            if (pl->health(i) != Plc::Health::HEALTHY)
                _emitf(out, sizeof(out), "  INVALID   %s", pl->name(i));
            else if (!pl->enabled(i))
                _emitf(out, sizeof(out), "  STOPPED   %s", pl->name(i));
        }
    }

    /** @brief `programs`'s verbs. @see CommandShell::Verb */
    const CommandShell::Verb* CommandShell::_programs_verbs(uint8_t& count) {
        static const Verb V[] = {
            { "status               how many, and which cannot run",          &CommandShell::_programs_show },
            { "list [<pat>]         name, or a glob",                         &CommandShell::_programs_list },
            { "start <pat|all>      put back in the scan, this run",          &CommandShell::_programs_start },
            { "stop  <pat|all>      take out, and drop what it drives safe",  &CommandShell::_programs_stop },
            { "enable  <pat|all>    start, and save it for the next boot",    &CommandShell::_programs_enable },
            { "disable <pat|all>    stop, and save it for the next boot",     &CommandShell::_programs_disable },
        };
        count = (uint8_t)(sizeof(V) / sizeof(V[0]));
        return V;
    }

}}  // namespace Botanik::Shell

#endif // BOTANIK_FEATURE_SHELL_PAGE && BOTANIK_FEATURE_PLC
