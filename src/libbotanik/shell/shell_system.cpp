/**
 * @file shell_system.cpp
 * @brief What the unit is DOING: log output, control authority, and the ways
 *        out (reset, reboot, exit).
 */
#include "libbotanik/shell/shell.h"         // CommandShell
#include "libbotanik/shell/args.h"          // arg_next / arg_eq / arg_is_all

#include <algorithm>                        // std::min
#include <cstdio>                           // snprintf
#include <cstring>                          // strlen / strncmp / memcpy

#include "libbotanik/hal/platform.h"        // HAL::System / Debug
#include "libbotanik/health/health.h"       // Health::HealthMonitor - `reboot`
#include "libbotanik/logs/logger.h"         // Logs::Logger::set_min_level
#include "libbotanik/network/ntp.h"         // Network::NtpClient - `ntpsync`
#include "libbotanik/vars/model.h"          // Vars::Bank - control authority

namespace Botanik { namespace Shell {

    void CommandShell::_cmd_logs(const char* args, uint16_t alen) {
        uint16_t wlen; const char* w = arg_next(args, alen, wlen);
        if (arg_eq(w, wlen, "status"))
            _println(_output.logs_enabled() ? "logs: output enabled"
                                            : "logs: output disabled");
        else if (arg_eq(w, wlen, "on")) {
            _output.set_logs_enabled(true);  _println("logs: output enabled");
        }
        else if (arg_eq(w, wlen, "off")) {
            _output.set_logs_enabled(false); _println("logs: output disabled");
        }
        else _verb_usage("logs");
    }

    void CommandShell::_cmd_breakpoint(const char*, uint16_t) { HAL::Debug::breakpoint(); }

    // Drop the field to its safe state before a deliberate restart: neither
    // reset() nor shutdown() promises anything about the outputs, and the gap
    // across a reboot is exactly when an actuator must not be left energised.
    void CommandShell::_leave_running() {
        // The system's hold, not the operator's: the firmware is deciding to
        // stop running, not someone turning control off.
        if (Vars::Bank* io = _bank()) {
            io->set_health_hold(true);
            // A deliberate stop loses no counting: the retain cadence bounds
            // what a power cut costs, a planned stop should cost nothing.
            (void)io->save_retained();
        }
        // Wipe the screen (flushed straight to the wire) before leaving: the
        // main loop won't run again to drain the TX ring.
        _terminal.clear_screen_now();
    }

    void CommandShell::_cmd_reset(const char*, uint16_t) {
        _leave_running();
        HAL::System::reset();      // reboots; does not return
    }

    // The two restarts differ in what they exercise, not in how hard they are.
    // `reset` cuts the loop off where it stands; `reboot` leaves it running and
    // withholds the kick, which is the path an unattended unit takes when it
    // wedges - the one worth being able to try on purpose. Control authority
    // goes FIRST either way: the gap before the reset is exactly when an
    // actuator must not be left energised.
    void CommandShell::_cmd_reboot(const char*, uint16_t) {
        if (_health == nullptr) {
            _println("reboot: health monitor unavailable (`reset` resets now)");
            return;
        }
        if (Vars::Bank* io = _bank()) {
            io->set_health_hold(true);
            (void)io->save_retained();
        }
        _health->request_reboot();
        // The loop keeps running until the watchdog fires, so unlike `reset`
        // this line reaches the screen and the log sinks.
        _println("reboot: outputs safe, watchdog kick withheld - resetting shortly");
    }

    void CommandShell::_cmd_exit(const char*, uint16_t) {
        _leave_running();
        HAL::System::shutdown();   // sim: exit(0); OPTA: reboots
    }

    void CommandShell::_cmd_control(const char* args, uint16_t alen) {
        Vars::Bank* io = _bank();
        if (io == nullptr) { _println("control: no variables"); return; }
        uint16_t w = 0;
        const char* a = arg_next(args, alen, w);

        if (arg_eq(a, w, "status")) { _report_control(io); return; }

        if (arg_eq(a, w, "enable")) {
            io->set_control_mode(Vars::CONTROL_ENABLED);
            _report_control(io);           // ...which the system may still veto
            return;
        }
        if (arg_eq(a, w, "inhibit")) {
            // The programs stop deciding, but `override` still reaches an
            // actuator - the whole reason to type this before working on the
            // plant.
            io->set_control_mode(Vars::CONTROL_INHIBITED);
            _println("control: INHIBITED - outputs at their safe state, scan "
                     "stopped; `overrides set` still drives them by hand");
            return;
        }
        if (arg_eq(a, w, "disable")) {
            io->set_control_mode(Vars::CONTROL_DISABLED);
            _println("control: DISABLED - outputs at their safe state, scan "
                     "stopped, output overrides ignored (kept, not cleared)");
            return;
        }
        _verb_usage("control");
    }

    // The operator's choice and the system's hold are reported separately: a
    // `control enable` that changes nothing must read as a health fault still in
    // force rather than a broken command.
    void CommandShell::_report_control(Vars::Bank* io) {
        const Vars::ControlMode eff = io->control_mode();
        const Vars::ControlMode op  = io->operator_mode();
        char out[160];
        if (!io->health_hold()) {
            _emitf(out, sizeof(out), "control: %s",
                   Vars::Bank::control_mode_name(eff));
            return;
        }
        // Held by the system: say so, and say what the operator asked for, so
        // releasing the fault has a predictable result.
        _emitf(out, sizeof(out),
               "control: %s - held by the system (bring-up, health, or a "
               "pending reset); operator asked for %s",
               Vars::Bank::control_mode_name(eff),
               Vars::Bank::control_mode_name(op));
    }

    void CommandShell::_cmd_ntpsync(const char*, uint16_t) {
        if (_ntp_client == nullptr) { _println("ntpsync: NTP unavailable"); return; }
        _ntp_client->resync();
        _println("ntpsync: NTP resync requested");
    }

}}  // namespace Botanik::Shell
