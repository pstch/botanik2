/**
 * @file shell_control.cpp
 * @brief `control`: the operator's standing choice about whether the laws drive.
 *
 * sys_control is one of two gates on a program, beside the unit's own run
 * state, and the stricter wins - so starting control on a unit that has not
 * reached RUN changes nothing until it does.
 *
 * start/stop are this run's; enable/disable are the same edit made durable, so
 * a unit that reboots comes up the way it was left. `programs start|stop` is
 * the per-program half, and has no durable form: the roster decides which
 * programs exist, and a unit coming up with one silently out of the scan is a
 * plant nobody can explain.
 */
#include "libbotanik/shell/shell.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_SHELL_PAGE
#include "libbotanik/shell/args.h"

#include "libbotanik/core/context.h"
#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/vars/model.h"
#include "libbotanik/vars/persist.h"
#include "libbotanik/vars/rows.h"
#if BOTANIK_FEATURE_PLC
#include "libbotanik/plc/runtime/runtime.h"
#endif

namespace Botanik { namespace Shell {

    void CommandShell::_cmd_control(const char* args, uint16_t alen) {
        uint8_t n; const Verb* V = _control_verbs(n);
        if (!_run_verb(V, n, args, alen)) _verb_usage("control");
    }

    // start/stop are this run; enable/disable are the same write plus the save,
    // which is the whole of what makes it durable.
    void CommandShell::_control_start(const char*, uint16_t)   { _control_set(true,  false); }
    void CommandShell::_control_stop(const char*, uint16_t)    { _control_set(false, false); }
    void CommandShell::_control_enable(const char*, uint16_t)  { _control_set(true,  true);  }
    void CommandShell::_control_disable(const char*, uint16_t) { _control_set(false, true);  }
    void CommandShell::_control_show(const char*, uint16_t)    { _control_status(); }

    /** @brief Write sys_control, and where @p persist commit it to storage.
     *  @param on      whether the laws are to drive
     *  @param persist false leaves the edit unsaved, so a reboot undoes it */
    void CommandShell::_control_set(bool on, bool persist) {
#if !defined(BOTANIK_SITE_ROWS)
        // No table: sys_control does not exist, so there is nothing to start.
        (void)on; (void)persist;
        _println("control: no variables");
        return;
#else
        Vars::Bank* io = _bank();
        char out[128];
        if (io == nullptr) { _println("control: no variables"); return; }

        Vars::Var c = io->var(Vars::Row::sys_control);
        if (c.param_set(on) != Vars::SET_OK) {
            _println("control: sys_control refused the write");
            return;
        }

        if (!persist) {
            _emitf(out, sizeof(out), "control %s  (this run only)",
                   on ? "started" : "STOPPED");
        } else {
            Vars::Persist* st = _store();
            if (st == nullptr) {
                _emitf(out, sizeof(out), "control %s, but there is nowhere to save it",
                       on ? "started" : "STOPPED");
            } else if (st->save(c) != Vars::SET_OK) {
                _emitf(out, sizeof(out), "control %s, but storage REFUSED the save",
                       on ? "started" : "STOPPED");
            } else {
                _emitf(out, sizeof(out), "control %s and saved",
                       on ? "ENABLED" : "DISABLED");
            }
        }
        if (!on)
            _println("  every law is out of the scan; what they drove is at its safe state");
        _control_status();
#endif
    }

    void CommandShell::_control_status() {
#if !defined(BOTANIK_SITE_ROWS)
        _println("control: no variables");
        return;
#else
        Vars::Bank* io = _bank();
        char out[160];
        if (io == nullptr) { _println("control: no variables"); return; }

        const bool on = io->var(Vars::Row::sys_control).as_bool();
        Scheduler::SchedulerContext* sc = (_sys != nullptr) ? _sys->scheduler : nullptr;
        const char* st = (sc != nullptr) ? Scheduler::run_state_name(sc->run_state)
                                         : "unknown";
        _emitf(out, sizeof(out), "control: %s     run state: %s",
               on ? "started" : "STOPPED", st);

#if BOTANIK_FEATURE_PLC
        Plc::Runtime::Plant* pl = (_sys != nullptr) ? _sys->plant : nullptr;
        if (pl != nullptr) {
            uint8_t run = 0;
            for (uint8_t i = 0; i < pl->count(); i++) if (pl->running(i)) run++;
            _emitf(out, sizeof(out), "programs: %u of %u running",
                   (unsigned)run, (unsigned)pl->count());
        }
#endif
        // The two gates are independent, and an operator who has just started
        // control on a unit that is still coming up needs telling why nothing
        // moved.
        if (on && sc != nullptr && sc->run_state != Scheduler::RunState::RUN)
            _println("  ...but the unit is not in RUN, so no law is driving yet");
#endif
    }

    /** @brief `control`'s verbs. @see CommandShell::Verb */
    const CommandShell::Verb* CommandShell::_control_verbs(uint8_t& count) {
        static const Verb V[] = {
            { "status               sys_control, and the run state beside it",  &CommandShell::_control_show },
            { "start                let the laws drive, this run",              &CommandShell::_control_start },
            { "stop                 stop every law, this run",                  &CommandShell::_control_stop },
            { "enable               start, and save it for the next boot",      &CommandShell::_control_enable },
            { "disable              stop, and save it for the next boot",       &CommandShell::_control_disable },
        };
        count = (uint8_t)(sizeof(V) / sizeof(V[0]));
        return V;
    }

}}  // namespace Botanik::Shell

#endif // BOTANIK_FEATURE_SHELL_PAGE
