/**
 * @file program.cpp
 * @brief Program's registration, plus the two reports the scan driver used to
 *        make about the table as a whole.
 */

#include "libbotanik/plc/program.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_PLC

#include <cstdio>

#include "libbotanik/logs/logger.h"
#include "libbotanik/logs/topics.h"
#include "libbotanik/tasks/tasks.h"

namespace Botanik { namespace Plc {

    void Program::_run_task(Component* c, const Tasks::Tick& t) {
        static_cast<Program*>(c)->_activate(t);
    }

    void Program::_bind_check(Vars::Bank& io, const char* name, BindDir dir,
                              bool (*accepts)(Vars::VarType)) {
        const Vars::VarRef r = io.find(name);

        // Every failure here is the same defect wearing three faces: the row
        // this program was written against is not the row it got. Logged as an
        // ERROR rather than a critical because the POST is what stops the
        // plant; this only says which name did it.
        const char* why = nullptr;
        char        extra[32];
        extra[0] = '\0';

        if (r.index == Vars::VAR_NONE) {
            why = "no such row";
        } else if (!accepts(io.var(r).meta()->type)) {
            why = "wrong type";
            snprintf(extra, sizeof extra, "row is %s",
                     Vars::type_name(io.var(r).meta()->type));
        } else if (dir == BIND_OUT) {
            // Before resolve_out() claims it, because claim_writer() hands the
            // row to the NEWEST writer even as it refuses: after the call every
            // loser looks like the owner.
            const uint8_t held = io.writer_of(r);
            if (held != Vars::WRITER_NONE && held != slot()) {
                why = "already driven";
                const Program* by = (_tasks != nullptr) ? _tasks->program(held)
                                                        : nullptr;
                snprintf(extra, sizeof extra, "by '%s'",
                         (by != nullptr) ? by->name() : "?");
            }
        }

        if (why != nullptr) {
            invalidate();
            if (_logger != nullptr)
                _logger->error(Logs::BOOT, "%s: cannot bind '%s'\n%s%s%s",
                               this->name(), name, why,
                               extra[0] != '\0' ? ", " : "", extra);
            return;
        }

        // Readable rows only, and only for the query that asks who consumes a
        // row. An output has a writer, which the bank records for itself.
        if (dir == BIND_OUT) return;
        if (_input_count >= Config::PROGRAM_MAX_INPUTS) { _input_overflow = true; return; }
        _inputs[_input_count++] = r;
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
            // Only on the CHANGE, which for a two-state vocabulary means once
            // at bring-up and never again. The quiet window exists so bring-up
            // does not announce every program, and INVALID is exempt: setup is
            // the only place that verdict is reached, so obeying quiet would
            // mean an unwireable program was never reported at all.
            if (logger != nullptr && (!quiet || h == Health::INVALID)) {
                if (h == Health::INVALID)
                    logger->critical(Logs::PROGRAM, "%s: INVALID", p->name());
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
