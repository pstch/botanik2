/**
 * @file shell_system.cpp
 * @brief What the unit is DOING: log output, control authority, and the ways
 *        out (reset, reboot, exit).
 */
#include "libbotanik/utils/minmax.h"
#include "libbotanik/shell/shell.h"

#include "libbotanik_features.h"

#if BOTANIK_FEATURE_SHELL_PAGE
#include "libbotanik/shell/args.h"

#include <cstdio>
#include <cstring>

#include "libbotanik/hal/platform.h"
#include "libbotanik/health/health.h"
#include "libbotanik/config/logging.gen.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/network/ntp.h"
#include "libbotanik/vars/model.h"

namespace Botanik { namespace Shell {

    void CommandShell::_cmd_logs(const char* args, uint16_t alen) {
        uint8_t n; const Verb* V = _logs_verbs(n);
        if (!_run_verb(V, n, args, alen)) _verb_usage("logs");
    }

    void CommandShell::_logs_on(const char*, uint16_t) {
        _output.set_logs_enabled(true);  _println("logs: output enabled");
    }

    void CommandShell::_logs_off(const char*, uint16_t) {
        _output.set_logs_enabled(false); _println("logs: output disabled");
    }

    void CommandShell::_logs_status(const char*, uint16_t) {
        _println(_output.logs_enabled() ? "logs: output enabled"
                                        : "logs: output disabled");
    }

    bool CommandShell::_confirmed(const char* args, uint16_t alen,
                                  const char* verb, const char* what) {
        uint16_t wlen; const char* w = arg_next(args, alen, wlen);
        if (w != nullptr && arg_eq(w, wlen, "confirm") &&
            arg_next(args, alen, wlen) == nullptr)
            return true;
        // One row at 80 columns, or the instruction is what wraps.
        char line[Config::LOG_FORMATTED_LEN];
        snprintf(line, sizeof(line), "%s: %s. type `%s confirm`", verb, what, verb);
        _println(line);
        return false;
    }

    void CommandShell::_cmd_breakpoint(const char*, uint16_t) {
        // A bare BKPT with nothing attached HardFaults the M7, so on a running
        // greenhouse this was the most destructive command on the console and
        // the only one that did not say so. hal/platform.h says to gate it.
        if (!HAL::Debug::under_debugger()) {
            _println("breakpoint: no debugger attached (it would fault the unit)");
            return;
        }
        HAL::Debug::breakpoint();
    }

    // Get what is worth keeping to flash before a deliberate restart. It does
    // NOT drop the field: the safe state is the lifecycle's, raised when a
    // program leaves the scan, and `reset`/`exit` do not go through it.
    void CommandShell::_leave_running() {
        // A deliberate stop loses no counting: the retain cadence bounds what a
        // power cut costs, a planned stop should cost nothing. The parameters a
        // PROGRAM owes ride with them, for the same reason.
        if (Vars::Persist* p = _store()) (void)p->flush(HAL::Clock::uptime_ms());
        // Wipe the screen (flushed straight to the wire) before leaving: the
        // main loop won't run again to drain the TX ring.
        _terminal.clear_screen_now();
    }

    void CommandShell::_cmd_reset(const char* args, uint16_t alen) {
        if (!_confirmed(args, alen, "reset", "resets the unit now, mid-beat")) return;
        _leave_running();
        HAL::System::reset();      // reboots; does not return
    }

    // The two restarts differ in what they exercise, not in how hard they are.
    // `reset` cuts the loop off where it stands; `reboot` leaves it running and
    // withholds the kick, which is the path an unattended unit takes when it
    // wedges - the one worth being able to try on purpose. Control authority
    // goes FIRST either way: the gap before the reset is exactly when an
    // actuator must not be left energised.
    void CommandShell::_cmd_reboot(const char* args, uint16_t alen) {
        if (!_confirmed(args, alen, "reboot",
                        "stops the laws, then the watchdog resets us")) return;
        if (_health == nullptr) {
            _println("reboot: health monitor unavailable (`reset` resets now)");
            return;
        }
        // The request moves the LIFECYCLE, it does not reset anything: the
        // next beat leaves RUN, which stops the laws, and sys_shutdown_sec
        // runs before the kick is withheld. That window is what gets the
        // retained rows and the owed parameters to flash.
        _health->request_reboot();

        // LOGGED rather than printed: a unit that reset is worth accounting for
        // afterwards, and the log is what the serial mirror and MQTT carry off
        // the unit. The loop keeps running until the watchdog fires - unlike
        // `reset`, which never returns - so there is time to drain it.
        //
        // CRITICAL because ShellPage lets one through the `logs off` gate, so
        // this IS the reply and a printed line beside it would say it twice.
        // The rate limiter applies to every level (logger.cpp), so what this
        // buys at the sink is a reserved FIFO slot, not exemption.
        const Scheduler::SchedulerContext* sc =
            (_sys != nullptr) ? _sys->scheduler : nullptr;
        if (_logger != nullptr) {
            if (sc != nullptr && sc->watchdog_ms != 0)
                _logger->critical(Logs::HEALTH,
                                  "reboot requested from the console\n"
                                  "stopping the laws, then %lu ms to reset",
                                  (unsigned long)sc->watchdog_ms);
            else
                _logger->critical(Logs::HEALTH,
                                  "reboot requested from the console\n"
                                  "stopping the laws, then the watchdog resets us");
        }
    }

    // The other ending: the same stop, and then the unit sits there. Kept
    // alive - the watchdog is still kicked, because the loop IS alive - and
    // doing nothing at all, including no console. Recovery is `reset`.
    void CommandShell::_cmd_halt(const char* args, uint16_t alen) {
        // The console goes with it, so the refusal is the last chance to say
        // that recovery is a physical reset.
        if (!_confirmed(args, alen, "halt",
                        "stops the cycle and this console. `reset` recovers")) return;
        if (_health == nullptr) {
            _println("halt: health monitor unavailable");
            return;
        }
        _health->request_stop(Health::HealthMonitor::Stop::HALT);
        if (_logger != nullptr)
            _logger->critical(Logs::HEALTH,
                              "halt requested; laws stop, then the cycle "
                              "(console too). `reset` recovers");
    }

    void CommandShell::_cmd_exit(const char* args, uint16_t alen) {
        // On the OPTA shutdown() IS reset(), so this is the fourth way to
        // restart the unit and guarding the other three without it would leave
        // the shortest one open.
        if (!_confirmed(args, alen, "exit", "resets the unit on hardware")) return;
        _leave_running();
        HAL::System::shutdown();   // sim: exit(0); OPTA: reboots
    }

    // An operator's own entry, on its own topic: what the shift wants recorded
    // beside what the firmware recorded itself, and it reaches every sink the
    // firmware's own lines do - the LOGS page, the serial mirror, MQTT.
    void CommandShell::_cmd_log(const char* args, uint16_t alen) {
        uint8_t n; const Verb* V = _log_verbs(n);
        if (!_run_verb(V, n, args, alen)) _verb_usage("log");
    }

    void CommandShell::_log_debug   (const char* a, uint16_t n) { _log_at(Logs::LOG_DEBUG,    a, n); }
    void CommandShell::_log_info    (const char* a, uint16_t n) { _log_at(Logs::LOG_INFO,     a, n); }
    void CommandShell::_log_notice  (const char* a, uint16_t n) { _log_at(Logs::LOG_NOTICE,   a, n); }
    void CommandShell::_log_warning (const char* a, uint16_t n) { _log_at(Logs::LOG_WARNING,  a, n); }
    void CommandShell::_log_error   (const char* a, uint16_t n) { _log_at(Logs::LOG_ERROR,    a, n); }
    void CommandShell::_log_critical(const char* a, uint16_t n) { _log_at(Logs::LOG_CRITICAL, a, n); }

    void CommandShell::_log_at(Logs::LogLevel level, const char* args, uint16_t alen) {
        while (alen > 0 && *args == ' ') { args++; alen--; }   // all of it is the text
        if (alen == 0) { _verb_usage("log", Logs::level_label(level)); return; }
        if (_logger == nullptr) { _println("log: no logger"); return; }

        // Bounded and NUL-terminated: the text is a slice of the command line,
        // and the logger takes a C string.
        char text[Config::LOG_MESSAGE_LEN];
        const uint16_t n = Utils::min(alen, (uint16_t)(sizeof(text) - 1));
        memcpy(text, args, n); text[n] = '\0';

        switch (level) {
            case Logs::LOG_DEBUG:    _logger->debug   (Logs::USER, "%s", text); break;
            case Logs::LOG_INFO:     _logger->info    (Logs::USER, "%s", text); break;
            case Logs::LOG_NOTICE:   _logger->notice  (Logs::USER, "%s", text); break;
            case Logs::LOG_WARNING:  _logger->warning (Logs::USER, "%s", text); break;
            case Logs::LOG_ERROR:    _logger->error   (Logs::USER, "%s", text); break;
            case Logs::LOG_CRITICAL: _logger->critical(Logs::USER, "%s", text); break;
        }
    }

#if BOTANIK_FEATURE_NTP
    void CommandShell::_cmd_ntpsync(const char*, uint16_t) {
        if (_ntp_client == nullptr) { _println("ntpsync: NTP unavailable"); return; }
        _ntp_client->resync();
        _println("ntpsync: NTP resync requested");
    }
#endif


    /** @brief `logs`'s verbs. @see CommandShell::Verb */
    const CommandShell::Verb* CommandShell::_logs_verbs(uint8_t& count) {
        static const Verb V[] = {
            { "on",      &CommandShell::_logs_on },
            { "off",     &CommandShell::_logs_off },
            { "status",  &CommandShell::_logs_status },
        };
        count = (uint8_t)(sizeof(V) / sizeof(V[0]));
        return V;
    }

    /** @brief `log`'s verbs. @see CommandShell::Verb */
    const CommandShell::Verb* CommandShell::_log_verbs(uint8_t& count) {
        static const Verb V[] = {
            { "debug <text>",     &CommandShell::_log_debug },
            { "info <text>",      &CommandShell::_log_info },
            { "notice <text>",    &CommandShell::_log_notice },
            { "warning <text>",   &CommandShell::_log_warning },
            { "error <text>",     &CommandShell::_log_error },
            { "critical <text>",  &CommandShell::_log_critical },
        };
        count = (uint8_t)(sizeof(V) / sizeof(V[0]));
        return V;
    }

}}  // namespace Botanik::Shell

#endif // BOTANIK_FEATURE_SHELL_PAGE
