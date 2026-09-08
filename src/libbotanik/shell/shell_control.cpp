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

#if BOTANIK_FEATURE_SHELL_PAGE && BOTANIK_FEATURE_VARS
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
        uint16_t wv; const char* verb = arg_next(args, alen, wv);

        if (arg_eq(verb, wv, "start"))   { _control_set(true,  false); return; }
        if (arg_eq(verb, wv, "stop"))    { _control_set(false, false); return; }
        if (arg_eq(verb, wv, "enable"))  { _control_set(true,  true);  return; }
        if (arg_eq(verb, wv, "disable")) { _control_set(false, true);  return; }
        if (arg_eq(verb, wv, "status"))  { _control_status();          return; }

        _verb_usage("control");
    }

    /** @brief Write sys_control, and where @p persist commit it to storage.
     *  @param on      whether the laws are to drive
     *  @param persist false leaves the edit unsaved, so a reboot undoes it */
    void CommandShell::_control_set(bool on, bool persist) {
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
    }

    void CommandShell::_control_status() {
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
    }

}}  // namespace Botanik::Shell

#endif // BOTANIK_FEATURE_SHELL_PAGE && BOTANIK_FEATURE_VARS
