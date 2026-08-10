/**
 * @file scan.cpp
 * @brief Scan driver: decide which programs are due and run them (see scan.h).
 */

#include "libbotanik/scan/scan.h"       // Scan::Driver

#include "libbotanik/config/scheduler.h"  // Config::LOOP_* / TASK_* / GROUP_*

#include "libbotanik/hal/platform.h"    // HAL::Clock
#include "libbotanik/logs/logger.h"     // Logs::Logger - naming a conflicting row

namespace Botanik { namespace Scan {

    void Driver::on_setup(uint64_t now_ms) {
        if (_programs == nullptr || _io == nullptr) return;

        // By name, once, like every other library consumer. A site that omitted
        // the library splice resolves to the sink and the POST names the row.
        _late_alarm = _io->find("alarm_scan_late");

        for (uint8_t i = 0; i < _count; i++) {
            Plc::Program* p = _programs[i];
            if (p == nullptr) continue;

            p->attach(*_io, _logger);
            // Its index in the site's table, which is what every row it writes
            // is stamped with. Here rather than in the program, because a
            // program cannot know where the site put it.
            p->set_slot(i);
            p->set_offset(Tasks::stagger_offset(i, Config::SCAN_STAGGER_MS,
                                                    p->period_ms()));
            // Resolves its names here, off the beat and before the watchdog is
            // armed, rather than lazily on its first step().
            p->setup(now_ms);
        }
    }

    void Driver::on_control(uint64_t now_ms) {
        if (_programs == nullptr || _io == nullptr) return;

        // Control authority gates the programs, and it lives here rather than at
        // the call site: the driver already holds the Bank, and the phase around
        // it must NOT be gated with it - the collector still has to carry the
        // safe states this withdrawal just wrote into the image out to the
        // field (see docs/dev/scan.md).
        if (!_io->control_enabled()) {
            if (_enabled) {
                // Falling edge: from here the process moves without us, so
                // whatever a program remembered about the field is stale. Each
                // program drops what has gone stale and keeps what is a record
                // (see plc/program.h).
                for (uint8_t i = 0; i < _count; i++)
                    if (_programs[i] != nullptr) _programs[i]->reset();
            }
            _enabled = false;
            return;
        }
        if (!_enabled) {
            // Control just came back. reseed() fixes the CLOCK: the first
            // activation reports its nominal period, not the whole outage (a
            // Ton holding 5 s handed an hour of dt fires the instant control
            // returns). It leaves a program that has never run alone, since its
            // first activation is due immediately by design - which is also the
            // bring-up case.
            //
            // resume() is the other half: it lets a program look at the plant
            // before commanding it again, and raises the first-scan flag so
            // even a program with nothing to re-datum knows its next run has no
            // previous activation to compare against.
            _enabled = true;
            for (uint8_t i = 0; i < _count; i++) {
                if (_programs[i] == nullptr) continue;
                _programs[i]->reseed(now_ms);
                _programs[i]->resume();
            }
        }

        // Due, how late, how many dropped: Component's arithmetic, applied by
        // step().
        for (uint8_t i = 0; i < _count; i++)
            if (_programs[i] != nullptr) _programs[i]->step(now_ms, Tasks::PHASE_CONTROL);

        _report_conflicts();
        _publish_lateness();
    }

    void Driver::_publish_lateness() {
        // The worst offender this beat, not a count: the question after "is the
        // scan keeping up" is always "which one".
        uint32_t worst = 0;
        _worst = nullptr;
        for (uint8_t i = 0; i < _count; i++) {
            const Plc::Program* p = _programs[i];
            if (p == nullptr || p->late_ms() <= worst) continue;
            worst  = p->late_ms();
            _worst = p;
        }
        _late_ms = worst;

        // Written every beat, like any other alarm input: the dwell in the row's
        // own policy is what turns a burst into an alarm, not this.
        _io->var(_late_alarm).set(worst > Config::SCAN_LATE_MS);
    }

    void Driver::_report_conflicts() {
        const uint16_t n = _io->writer_conflicts();
        if (n == _conflicts_said || _logger == nullptr) return;

        // Edge-triggered and named once. Two programs driving one output is a
        // design defect, true on every beat from here, so a per-beat line would
        // bury the log.
        const Vars::VarRef r = _io->first_conflict();
        const Vars::Var    v = _io->var(r);
        const uint8_t      w = _io->writer_of(r);
        const char* other = (w < _count && _programs[w] != nullptr)
                                ? _programs[w]->name() : "?";
        // Count first: both names are variable-width (a row name runs to 23
        // characters), so either leading would overrun the line.
        _logger->critical(Logs::STATUS,
                          "scan: %u row(s) with 2+ writers\n"
                          "'%s' driven by '%s'",
                          (unsigned)n,
                          v.name() != nullptr ? v.name() : "?", other);
        _conflicts_said = n;
    }

}}  // namespace Botanik::Scan
