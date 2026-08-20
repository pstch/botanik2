/**
 * @file health.cpp
 * @brief HealthMonitor's registration: its own task, then the alarm model's.
 */

#include "libbotanik/health/health.h"

#include "libbotanik/plc/program.h"
#include "libbotanik/tasks/tasks.h"

#if BOTANIK_FEATURE_NETWORK
#include "libbotanik/network/network.h"
#endif
#if BOTANIK_FEATURE_NTP
#include "libbotanik/network/ntp.h"
#endif

namespace Botanik { namespace Health {

    /** @brief One name per PlatAlarm, in enum order. Nothing else names them,
     *  so this list IS the list: no second copy to drift from. */
    const char* const HealthMonitor::PLATFORM_NAMES[HealthMonitor::PA_COUNT] = {
        "alarm_system_reset",
        "alarm_cpu_xrun",
        "alarm_cpu_load",
        "alarm_stack",
        "alarm_cpu_jitter",
        "alarm_flash_error",
    };

    /** @brief The registry hands back the owner it was given; the cast is
     *  confined to this line, beside the method it calls. */
#if BOTANIK_FEATURE_VARS
    void HealthMonitor::_subsystem_alarms(Vars::Bank* io) {
        // Each condition comes off the component that owns it, so there is no
        // second copy of "what counts as broken" here. A row a build does not
        // declare resolves to the sink and the write goes nowhere.
#if BOTANIK_FEATURE_NETWORK
        if (_net != nullptr) io->var(_net_fault).set(!_net->is_up());
#endif
#if BOTANIK_FEATURE_NTP
        if (_ntp != nullptr) io->var(_ntp_late).set(!_ntp->synced());
#endif
#if BOTANIK_FEATURE_LOGGING
        if (_logger != nullptr) {
            // MOVING, not large: a boot that dropped a burst three hours ago is
            // not a flood now, and the row's hold folds a burst into one
            // episode.
            const uint32_t dropped = _logger->dropped_total();
            io->var(_log_flood).set(dropped != _dropped_seen);
            _dropped_seen = dropped;
        }
#endif
    }
#endif

    void HealthMonitor::_service_task(Component* c, const Tasks::Tick& t) {
        static_cast<HealthMonitor*>(c)->on_service(t.now_ms);
    }

#if BOTANIK_FEATURE_VARS
    void HealthMonitor::_latch_task(Component* c, const Tasks::Tick& t) {
        static_cast<HealthMonitor*>(c)->_latch_control(t.now_ms);
    }

    // Control authority is the Bank's latch, so there is none to carry without
    // one. A build with no variable model has a watchdog verdict and alarms,
    // and nothing to withdraw authority FROM.
    void HealthMonitor::_latch_control(uint64_t now_ms) {
        Vars::Bank* io = const_cast<Vars::Bank*>(_io);
        if (io == nullptr) return;

        if (!watchdog_ok()) {
            if (!_health_fault) {
                _health_fault = true;
                io->set_health_hold(true);
                if (_logger) _logger->critical(Logs::HEALTH,
                    "control disabled\n"
                    "outputs at their safe state, scan inhibited");
            }
        } else if (_health_fault) {
            _health_fault = false;
            io->set_health_hold(false);
            // Lead hoisted out of the ternary: the format string stays a
            // literal the printf attribute can check, and both outcomes grep
            // as "health recovered".
            if (_logger) _logger->warning(Logs::HEALTH, "health recovered\n%s",
                io->control_enabled()
                    ? "control enabled, scan resuming"
                    : "control still disabled by the operator");
        }

        // Taking the unit out of service, if that has been asked for. FIRST,
        // because it decides what control authority may be this beat.
        if (_stop != Stop::NONE && !_stopped && _tasks != nullptr) {
            // Every program drives its own actuators to their safe positions
            // and says when it has arrived. It keeps being ACTIVATED while it
            // does: parking is work, and a program that is not run cannot close
            // a valve or park a window.
            _tasks->request_safe(true);
            if (_stop_since == 0) _stop_since = now_ms;
            const bool out_of_time =
                (now_ms - _stop_since) >= Config::STOP_PARK_MS;
            if (out_of_time && _logger && !_stopped)
                _logger->error(Logs::HEALTH,
                               "%s: %u program(s) never parked, going anyway",
                               _stop == Stop::REBOOT ? "reboot" : "halt",
                               (unsigned)_tasks->unsafe_count());
            if (_tasks->all_safe() || out_of_time) {
                _stopped = true;
                // Only now: the field goes to its declared safe state, the
                // retained rows are written, and whichever of the two endings
                // was asked for takes effect.
                io->set_health_hold(true);
                (void)io->save_retained();
                if (_logger) _logger->warning(Logs::HEALTH,
                    "%s: plant safe",
                    _stop == Stop::REBOOT ? "reboot" : "halt");
            } else if (_logger && _stop_said != _tasks->unsafe_count()) {
                _stop_said = _tasks->unsafe_count();
                _logger->notice(Logs::HEALTH, "%s: %u program(s) still parking",
                                _stop == Stop::REBOOT ? "reboot" : "halt",
                                (unsigned)_stop_said);
            }
        }

        // The whole authority edge, in one idempotent call: the gate, and on
        // the way back up the timer reseed and the first-scan flag.
        if (_tasks != nullptr)
            _tasks->set_programs_enabled(io->control_enabled(), now_ms);

#if BOTANIK_FEATURE_LOGGING
        // Before the PLC block below, and outside it: whether a reading is
        // believable has nothing to do with whether this build runs programs.
        _quality_sweep();
#endif

        _subsystem_alarms(io);

#if BOTANIK_FEATURE_PLC
        if (_tasks == nullptr) return;
        // SCAN_LATE is a platform alarm like the six bound above: written every
        // beat, and the row's own dwell decides whether it annunciates.
        const Plc::Program* worst = nullptr;
        const uint32_t late = Plc::worst_lateness(*_tasks, &worst);
        io->var(_scan_late).set(late > Config::SCAN_LATE_MS);
        // Edge-triggered and named once: two programs driving one output is a
        // design defect, true on every beat from here.
        (void)Plc::report_writer_conflicts(*io, *_tasks, _logger, _conflicts_said);
        // Every program's own verdict onto its row, and the changes into the
        // log. Here rather than in each program's activation, because a program
        // that is NOT being activated is exactly when a reader wants the row.
        Plc::publish_health(*io, *_tasks, _logger);
#endif
    }
#endif // BOTANIK_FEATURE_VARS

#if BOTANIK_FEATURE_VARS && BOTANIK_FEATURE_LOGGING
    void HealthMonitor::_quality_sweep() {
        // Enough to cross a 500-row table in about a second at GROUP_SLOW: fast
        // enough that a reader watching a sensor die sees it, slow enough that
        // it is not a per-beat walk of the whole model.
        static const uint16_t PER_BEAT = 48;
        const Vars::Bank* io = _io;
        if (io == nullptr) return;

        for (uint16_t n = 0; n < PER_BEAT && io->count() > 0; n++) {
            if (_quality_at >= io->count()) {
                _quality_at = 0;
                _quality_seeded = true;      // one full pass is now behind us
            }
            const uint16_t i = _quality_at++;
            const bool now  = io->var(i).valid();
            uint8_t& byte   = _was_valid[i / 8];
            const uint8_t bit = (uint8_t)(1u << (i % 8));
            const bool was  = (byte & bit) != 0;
            if (now == was) continue;
            byte = (uint8_t)(now ? (byte | bit) : (byte & ~bit));
            if (!_quality_seeded || _logger == nullptr) continue;
            const char* name = io->var(i).name();
            _logger->debug(Logs::QUALITY, "%s: %s", name != nullptr ? name : "?",
                           now ? "valid" : "invalid");
        }
    }
#endif

    void HealthMonitor::register_tasks() {
        _tasks->add(*this, &HealthMonitor::_service_task, "health", Tasks::GROUP_SLOW,
              Tasks::PHASE_SERVICE);
#if BOTANIK_FEATURE_VARS
        // Every beat, and BEFORE the programs: a fault must inhibit this beat's
        // control, not the next one.
        _tasks->add(*this, &HealthMonitor::_latch_task, "control_latch",
                    Tasks::GROUP_FAST, Tasks::PHASE_SERVICE);
#endif
        // Then the model, which this used to step inside its own task: same
        // order, but with a rate and a TASKS row of its own.
        _model.register_tasks();
    }

}}  // namespace Botanik::Health
