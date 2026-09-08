/**
 * @file shell_control.cpp
 * @brief `control`: the operator's standing choice about whether the laws drive.
 *
 * sys_control is one of two gates on a program, beside the unit's own run
 * state, and the stricter wins - so `control start` on a unit that has not
 * reached RUN changes nothing until it does. `programs disable` is the same
 * choice for one program.
 */
#include "libbotanik/shell/shell.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_SHELL_PAGE && BOTANIK_FEATURE_VARS
#include "libbotanik/shell/args.h"

#include "libbotanik/core/context.h"
#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/vars/model.h"
#include "libbotanik/vars/rows.h"
#if BOTANIK_FEATURE_PLC
#include "libbotanik/plc/runtime/runtime.h"
#endif

namespace Botanik { namespace Shell {

    void CommandShell::_cmd_control(const char* args, uint16_t alen) {
        uint16_t wv; const char* verb = arg_next(args, alen, wv);

        if (arg_eq(verb, wv, "start"))  { _control_set(true);  return; }
        if (arg_eq(verb, wv, "stop"))   { _control_set(false); return; }
        if (arg_eq(verb, wv, "status")) { _control_status();   return; }

        _verb_usage("control");
    }

    /** @brief Write sys_control. Live and unsaved, as any parameter edit is:
     *  a unit that reboots comes up on what was last saved, which is what an
     *  operator inhibiting control for an afternoon wants. */
    void CommandShell::_control_set(bool on) {
        Vars::Bank* io = _bank();
        char out[128];
        if (io == nullptr) { _println("control: no variables"); return; }

        Vars::Var c = io->var(Vars::Row::sys_control);
        if (c.param_set(on) != Vars::SET_OK) {
            _println("control: sys_control refused the write");
            return;
        }
        _emitf(out, sizeof(out), "control %s  (unsaved: `params save sys_control`)",
               on ? "started" : "STOPPED");
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
