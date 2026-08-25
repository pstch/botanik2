/**
 * @file program.cpp
 * @brief Program's registration, plus the two reports the scan driver used to
 *        make about the table as a whole.
 */

#include "libbotanik/plc/program.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_PLC

#include "libbotanik/logs/logger.h"
#include "libbotanik/logs/topics.h"
#include "libbotanik/tasks/tasks.h"

namespace Botanik { namespace Plc {

    void Program::_run_task(Component* c, const Tasks::Tick& t) {
        static_cast<Program*>(c)->_activate(t);
    }

    void Program::register_tasks() {
        // No phase to name: a program runs in PHASE_CONTROL or not at all, and
        // add_program() marks the entry so the authority gate can find it.
        _tasks->add_program(*this, &Program::_run_task, name(), _group);
    }

    uint32_t worst_lateness(const Tasks::Registry& r, const Program** worst) {
        // The worst offender, not a count: the question after "is the scan
        // keeping up" is always "which one".
        uint32_t late = 0;
        if (worst != nullptr) *worst = nullptr;
        const uint8_t count = r.program_count();
        for (uint8_t i = 0; i < count; i++) {
            const Program* p = r.program(i);
            if (p == nullptr || p->late_ms() <= late) continue;
            late = p->late_ms();
            if (worst != nullptr) *worst = p;
        }
        return late;
    }

    void publish_health(Vars::Bank& io, const Tasks::Registry& r,
                        Logs::Logger* logger, bool quiet) {
        const uint8_t count = r.program_count();
        for (uint8_t i = 0; i < count; i++) {
            Program* p = r.program(i);
            if (p == nullptr) continue;
            const uint8_t now = (uint8_t)p->health();
            io.drive(p->health_row(), now);
            if (now == p->_health_said) continue;
            const Health h = p->health();
            // Only on the CHANGE: a program reporting DEGRADED every beat for
            // an hour is one line here and a flood anywhere else.
            //
            // A program stopping or resuming control is a NOTICE and not a
            // debug line, because on the OPTA the minimum level is INFO: below
            // it the only record that a plant went to its safe state would be
            // one the hardware never keeps. The whole set of them lands on one
            // operator command, which is a rare and deliberate act.
            //
            // The quiet window exists so bring-up does not announce the first
            // verdict of every program, and FAULTED is exempt from it: setup is
            // the only place that verdict is reached, so obeying quiet would
            // mean an unwireable program was never reported at all.
            if (logger != nullptr && (!quiet || h == Health::FAULTED)) {
                if (h == Health::FAULTED)
                    logger->critical(Logs::PROGRAM, "%s: FAULTED", p->name());
                else if (h == Health::DEGRADED)
                    logger->warning(Logs::PROGRAM, "%s: DEGRADED", p->name());
                else
                    logger->notice(Logs::PROGRAM, "%s: %s", p->name(),
                                   health_name(h));
            }
            p->_health_said = now;
        }
    }

    bool report_writer_conflicts(Vars::Bank& io, const Tasks::Registry& reg,
                                 Logs::Logger* logger, uint16_t& said) {
        const uint16_t n = io.writer_conflicts();
        if (n == said || logger == nullptr) return false;

        const Vars::VarRef r = io.first_conflict();
        const Vars::Var    v = io.var(r);
        const uint8_t      w = io.writer_of(r);
        const Program* by = reg.program(w);
        const char* other = (by != nullptr) ? by->name() : "?";
        // Count first: both names are variable-width (a row name runs to 23
        // characters), so either leading would overrun the line.
        logger->critical(Logs::STATUS,
                         "scan: %u row(s) with 2+ writers\n"
                         "'%s' driven by '%s'",
                         (unsigned)n,
                         v.name() != nullptr ? v.name() : "?", other);
        said = n;
        return true;
    }

}}  // namespace Botanik::Plc

#endif // BOTANIK_FEATURE_PLC
