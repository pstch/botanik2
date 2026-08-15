/**
 * @file shell_system.cpp
 * @brief What the unit is DOING: log output, control authority, and the ways
 *        out (reset, reboot, exit).
 */
#include "libbotanik/shell/shell.h"
#include "libbotanik/shell/args.h"

#include <algorithm>
#include <cstdio>
#include <algorithm>
#include <cstring>

#include "libbotanik/hal/platform.h"
#include "libbotanik/health/health.h"
#include "libbotanik/config/logging.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/network/ntp.h"
#include "libbotanik/vars/model.h"

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

        // LOGGED rather than printed: a unit that reset is worth accounting for
        // afterwards, and the log is what the serial mirror and MQTT carry off
        // the unit. The loop keeps running until the watchdog fires - unlike
        // `reset`, which never returns - so there is time to drain it.
        //
        // CRITICAL does both jobs: the rate limiter cannot drop it on the way
        // out, and ShellPage lets a critical through the `logs off` gate - so
        // this IS the reply, on screen either way, and a printed line beside it
        // would only say the same thing twice.
        const Scheduler::SchedulerContext* sc =
            (_sys != nullptr) ? _sys->scheduler : nullptr;
        if (_logger != nullptr) {
            if (sc != nullptr && sc->watchdog_ms != 0)
                _logger->critical(Logs::HEALTH,
                                  "reboot requested from the console\n"
                                  "outputs safe, kick withheld: resets within %lu ms",
                                  (unsigned long)sc->watchdog_ms);
            else
                _logger->critical(Logs::HEALTH,
                                  "reboot requested from the console\n"
                                  "outputs safe, kick withheld: resets when the watchdog expires");
        }
    }

    void CommandShell::_cmd_exit(const char*, uint16_t) {
        _leave_running();
        HAL::System::shutdown();   // sim: exit(0); OPTA: reboots
    }

    // An operator's own entry, on its own topic: what the shift wants recorded
    // beside what the firmware recorded itself, and it reaches every sink the
    // firmware's own lines do - the LOGS page, the serial mirror, MQTT.
    void CommandShell::_cmd_log(const char* args, uint16_t alen) {
        uint16_t wl; const char* lvl = arg_next(args, alen, wl);
        while (alen > 0 && *args == ' ') { args++; alen--; }   // the rest is the text
        if (lvl == nullptr || alen == 0) { _verb_usage("log"); return; }
        if (_logger == nullptr) { _println("log: no logger"); return; }

        // Bounded and NUL-terminated: the text is a slice of the command line,
        // and the logger takes a C string.
        char text[Config::LOG_MESSAGE_LEN];
        const uint16_t n = std::min(alen, (uint16_t)(sizeof(text) - 1));
        memcpy(text, args, n); text[n] = '\0';

        if      (arg_eq(lvl, wl, "debug"))    _logger->debug   (Logs::USER, "%s", text);
        else if (arg_eq(lvl, wl, "info"))     _logger->info    (Logs::USER, "%s", text);
        else if (arg_eq(lvl, wl, "notice"))   _logger->notice  (Logs::USER, "%s", text);
        else if (arg_eq(lvl, wl, "warning"))  _logger->warning (Logs::USER, "%s", text);
        else if (arg_eq(lvl, wl, "error"))    _logger->error   (Logs::USER, "%s", text);
        else if (arg_eq(lvl, wl, "critical")) _logger->critical(Logs::USER, "%s", text);
        else { _verb_usage("log"); return; }
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
