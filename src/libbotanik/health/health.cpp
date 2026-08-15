/**
 * @file health.cpp
 * @brief HealthMonitor's registration: its own task, then the alarm model's.
 */

#include "libbotanik/health/health.h"

#include "libbotanik/plc/program.h"
#include "libbotanik/tasks/tasks.h"

namespace Botanik { namespace Health {

    /** @brief The registry hands back the owner it was given; the cast is
     *  confined to this line, beside the method it calls. */
    void HealthMonitor::_service_task(Component* c, const Tasks::Tick& t) {
        static_cast<HealthMonitor*>(c)->on_service(t.now_ms);
    }

    void HealthMonitor::_latch_task(Component* c, const Tasks::Tick& t) {
        static_cast<HealthMonitor*>(c)->_latch_control(t.now_ms);
    }

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

        // The whole authority edge, in one idempotent call: the gate, and on
        // the way back up the timer reseed and the first-scan flag.
        if (_tasks != nullptr)
            _tasks->set_programs_enabled(io->control_enabled(), now_ms);

#if BOTANIK_FEATURE_PLC_SCAN
        if (_tasks == nullptr) return;
        // SCAN_LATE is a platform alarm like the six bound above: written every
        // beat, and the row's own dwell decides whether it annunciates.
        const Plc::Program* worst = nullptr;
        const uint32_t late = Plc::worst_lateness(*_tasks, &worst);
        io->var(_scan_late).set(late > Config::SCAN_LATE_MS);
        // Edge-triggered and named once: two programs driving one output is a
        // design defect, true on every beat from here.
        (void)Plc::report_writer_conflicts(*io, *_tasks, _logger, _conflicts_said);
#endif
    }

    void HealthMonitor::register_tasks() {
        _tasks->add(*this, &HealthMonitor::_service_task, "health", Tasks::GROUP_SLOW,
              Tasks::PHASE_SERVICE);
        // Every beat, and BEFORE the programs: a fault must inhibit this beat's
        // control, not the next one.
        _tasks->add(*this, &HealthMonitor::_latch_task, "control_latch",
                    Tasks::GROUP_FAST, Tasks::PHASE_SERVICE);
        // Then the model, which this used to step inside its own task: same
        // order, but with a rate and a TASKS row of its own.
        _model.register_tasks();
    }

}}  // namespace Botanik::Health
