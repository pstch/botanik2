/**
 * @file health.cpp
 * @brief HealthMonitor's registration: its own task, then the alarm model's.
 */

#include "libbotanik/plc/health.h"
#include "libbotanik/health/health.h"


#include "libbotanik/tasks/tasks.h"

#if BOTANIK_FEATURE_PLC
#include "libbotanik/plc/runtime/runtime.h"
#endif
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

#if !BOTANIK_FEATURE_VARS
    /** @brief The same six as the library's own SCOPE_A rows carry, for a build
     *  with no table to read them out of. @see vars/tables/library.csv */
    const char* const HealthMonitor::PLATFORM_DESCS[HealthMonitor::PA_COUNT] = {
        "unit reset recently",
        "scheduler overrun (skipped beat)",
        "CPU load over threshold",
        "stack usage over threshold",
        "CPU jitter over threshold",
        "persistent storage write failed",
    };

    const Alarms::AlarmSpec
    HealthMonitor::PLATFORM_SPECS[HealthMonitor::PA_COUNT] = {
        { "SYS_RST"  , -1, 0 , 30*24*3600, 0   },
        { "CPU_XRUN" , 0 , -1, 24*3600   , 300 },
        { "CPU_LOAD" , 5 , 5 , 24*3600   , 300 },
        { "STACK"    , 5 , 5 , 30*24*3600, 300 },
        { "CPU_JITT" , 5 , 5 , 24*3600   , 300 },
        { "FLASH_FLT", 0 , -1, 30*24*3600, 0   },
    };
#endif

    /** @brief The registry hands back the owner it was given; the cast is
     *  confined to this line, beside the method it calls. */
#if BOTANIK_FEATURE_VARS
    void HealthMonitor::_subsystem_alarms(Vars::Bank* io) {
        // Each condition comes off the component that owns it, so there is no
        // second copy of "what counts as broken" here. A row a build does not
        // declare resolves to the sink and the write goes nowhere.
        //
        // failed()/late() rather than !is_up()/!synced(): a subsystem still
        // coming up is not broken, and one nobody enabled is not broken either
        // - both of which "not up" says, once per boot and for ever after
        // net_enable is turned off.
#if BOTANIK_FEATURE_NETWORK
        if (_net != nullptr) io->var(_net_fault).set(_net->failed());
#endif
#if BOTANIK_FEATURE_NTP
        if (_ntp != nullptr) io->var(_ntp_late).set(_ntp->late());
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


    void HealthMonitor::_latch_task(Component* c, const Tasks::Tick& t) {
        static_cast<HealthMonitor*>(c)->_latch_control(t.now_ms);
    }

    // Two halves with different dependencies. The ENDING is the lifecycle's and
    // runs in every build: `halt` and `reboot` are answers the unit owes whether
    // or not it has a table. Control authority is the Bank's latch, so there is
    // none to carry without one, and the rest below needs the bank.
    void HealthMonitor::_latch_control(uint64_t now_ms) {
        // Taking the unit out of service, if that has been asked for. There is
        // no handshake to wait on, because a program has no state to reach.
        if (_stop != Stop::NONE && !_stopped) {
            _stopped = true;
#if BOTANIK_FEATURE_VARS
            if (Vars::Bank* store = _io) {
                (void)store->save_retained();
                // ...and the parameters a PROGRAM owes, which are the same kind
                // of fact and reach the store the same way: a watering date
                // stamped since the last cadence would otherwise be lost by the
                // one exit that is supposed to lose nothing. NOT the operator's
                // unsaved edits - those stay theirs to confirm.
                (void)store->save_owed_params();
                if (_logger) _logger->warning(Logs::HEALTH, "%s: retained saved",
                    _stop == Stop::REBOOT ? "reboot" : "halt");
            }
#endif
        }

#if !BOTANIK_FEATURE_VARS
        (void)now_ms;
    }
#else
        Vars::Bank* io = _io;
        if (io == nullptr) return;

#if BOTANIK_FEATURE_LOGGING
        // Before the PLC block below, and outside it: whether a reading is
        // believable has nothing to do with whether this build runs programs.
        _quality_sweep(now_ms);
#endif

        _subsystem_alarms(io);

#if BOTANIK_FEATURE_PLC
        if (_tasks == nullptr) return;
        // SCAN_LATE is a platform alarm like the six bound above: written every
        // beat, and the row's own dwell decides whether it annunciates.
        // MISSED ACTIVATIONS, not milliseconds late. The registry counts what
        // TaskTimer dropped, which is what "a control program is missing its
        // period" means; the millisecond figure came off Plc::Program, and
        // there have been none of those since the ST migration - so this alarm
        // was reading a constant zero.
        uint32_t missed = 0;
        for (uint8_t i = 0; i < _tasks->count(); i++) {
            const Tasks::Task& t = _tasks->task(i);
            if (t.phase == Tasks::PHASE_CONTROL) missed += t.cost.missed;
        }
        io->var(_scan_late).set(missed != _missed_said);
        _missed_said = missed;
        // The per-program verdict is published by Plc::Runtime::Plant::wire(),
        // which is the only place it is decided: a program either binds every
        // name at bring-up or never runs.
#endif
    }
#endif // BOTANIK_FEATURE_VARS

#if BOTANIK_FEATURE_VARS && BOTANIK_FEATURE_LOGGING
    void HealthMonitor::_quality_sweep(uint64_t now_ms) {
        // Enough to cross a 500-row table in about a second at HEALTH_PERIOD_MS: fast
        // enough that a reader watching a sensor die sees it, slow enough that
        // it is not a per-beat walk of the whole model.
        static const uint16_t PER_BEAT = 48;
        const Vars::Bank* io = _io;
        if (io == nullptr) return;

#if BOTANIK_FEATURE_PLC
        // A program joining or leaving the scan churns the quality of every row
        // it drives: they drop to their safe state on the way out and are
        // re-authored on the way back. That is settling, like bring-up, so it
        // is adopted the same way - the bitmap below still tracks it, so the
        // NEXT change is announced.
        if (_plant != nullptr) {
            const uint32_t mask = _plant->running_mask();
            if (mask != _scan_mask) {
                _scan_mask     = mask;
                _hush_until_ms = now_ms + Config::HEALTH_QUIET_MS;
            }
        }
#endif

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
            // Bring-up is not news: every field row goes valid as its device
            // first answers, and announcing all of it is a hundred lines in one
            // breath. The bitmap is updated above either way, so a row adopted
            // quietly still announces the NEXT change.
            if (!_quality_seeded || _logger == nullptr || _quiet(now_ms)) continue;
            const char* name = io->var(i).name();
            if (now) _logger->debug(Logs::QUALITY, "%s: valid",
                                    name != nullptr ? name : "?");
            // A reading going away is a WARNING: something the plant was being
            // controlled on has stopped being measured, and the recovery is
            // just the record closing.
            else _logger->warning(Logs::QUALITY, "%s: invalid",
                                  name != nullptr ? name : "?");
        }
    }
#endif

    void HealthMonitor::register_tasks() {
        _tasks->add(*this, "sys_health", Config::HEALTH_PERIOD_MS,
              Tasks::PHASE_SERVICE);
        // Every beat, and BEFORE the programs: a fault must inhibit this beat's
        // control, not the next one. Registered whatever the build, because the
        // same task latches the ENDING - gated on VARS, `halt` and `reboot`
        // were accepted, latched, and never happened.
        _tasks->add(*this, &HealthMonitor::_latch_task, "sys_control_latch",
                    Tasks::EVERY_BEAT, Tasks::PHASE_SERVICE);
        // Then the model, in its own task: same order, with a rate and a
        // TASKS row of its own.
        _model.register_tasks();
    }

}}  // namespace Botanik::Health
