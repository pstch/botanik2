/**
 * @file program.cpp
 * @brief Program's registration, and the two control-authority edges that used
 *        to live in the scan driver, which is gone.
 */

#include "libbotanik/plc/program.h"     // Plc::Program

#include "libbotanik/logs/logger.h"     // Logs::Logger - naming a conflicting row
#include "libbotanik/logs/topics.h"     // Logs::STATUS
#include "libbotanik/tasks/tasks.h"     // Tasks::Registry / Tick / GroupId

namespace Botanik { namespace Plc {

    void Program::_run_task(Component* c, const Tasks::Tick& t) {
        static_cast<Program*>(c)->_activate(t);
    }

    void Program::register_tasks(Tasks::Registry& r) {
        // No phase to name: a program runs in PHASE_CONTROL or not at all, and
        // add_program() marks the entry so the authority gate can find it.
        r.add_program(*this, &Program::_run_task, name(), _group);
    }

    void apply_control_edge(Program* const* programs, uint8_t count, bool granted) {
        if (programs == nullptr) return;
        for (uint8_t i = 0; i < count; i++) {
            if (programs[i] == nullptr) continue;
            if (granted) programs[i]->resume();
            else         programs[i]->reset();
        }
    }

    uint32_t worst_lateness(Program* const* programs, uint8_t count,
                            const Program** worst) {
        // The worst offender, not a count: the question after "is the scan
        // keeping up" is always "which one".
        uint32_t late = 0;
        if (worst != nullptr) *worst = nullptr;
        if (programs == nullptr) return 0;
        for (uint8_t i = 0; i < count; i++) {
            const Program* p = programs[i];
            if (p == nullptr || p->late_ms() <= late) continue;
            late = p->late_ms();
            if (worst != nullptr) *worst = p;
        }
        return late;
    }

    bool report_writer_conflicts(Vars::Bank& io, Program* const* programs,
                                 uint8_t count, Logs::Logger* logger,
                                 uint16_t& said) {
        const uint16_t n = io.writer_conflicts();
        if (n == said || logger == nullptr) return false;

        const Vars::VarRef r = io.first_conflict();
        const Vars::Var    v = io.var(r);
        const uint8_t      w = io.writer_of(r);
        const char* other = (programs != nullptr && w < count &&
                             programs[w] != nullptr) ? programs[w]->name() : "?";
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
