/**
 * @file runtime.cpp
 * @brief HAND-WRITTEN - the join between the beat and the generated ST.
 */

#include "libbotanik/plc/runtime/runtime.h"
#include "libbotanik/config/base.h"
#include "libbotanik/config/scheduler.h"

#include <cstdio>
#include "libbotanik/plc/runtime/bank.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/utils/assert.h"
#include "libbotanik/hal/platform.h"

#if BOTANIK_FEATURE_PLC

namespace Botanik { namespace Plc { namespace Runtime {

    
    void Plant::on_setup(uint64_t now_ms) {
        (void)now_ms;
        wire();
    }

    void Plant::wire() {
        io = &_io;
        _now_ref = _io.find("sys_now_ms");
        _day_ref = _io.find("sys_today");
        BOTANIK_ASSERT(PROGRAM_COUNT <=
                       (uint8_t)(sizeof _slot / sizeof _slot[0]));

        char row[40];
        for (uint8_t i = 0; i < PROGRAM_COUNT; i++) {
            const Program& p = programs[i];

            // Its OWN window, and its own writer id: two programs claiming one
            // row is caught here exactly as it was when these were C++, which
            // is the check ST had no way to keep on its own.
            const uint16_t missed = _io.unresolved_count();
            _io.begin_writer(i);
            p.init();
            _io.end_writer();

            // Counted per program, so a name one of them wanted and did not
            // get holds back that one and nothing else.
            const bool wired = (_io.unresolved_count() == missed);
            _slot[i].health = wired ? Health::HEALTHY
                                    : Health::INVALID;

            const int n = snprintf(row, sizeof row, "prog_%s", p.name);
            if (n > 0 && (size_t)n < sizeof row) {
                const Vars::VarRef r = _io.find(row, (uint16_t)n);
                _slot[i].health_row = r;
                if (r.index != Vars::VAR_NONE)
                    _io.var(r).set_i((int64_t)_slot[i].health);
            }
            if (!wired && _logger != nullptr)
                _logger->critical(Logs::BOOT,
                                  "%s: a name it binds resolves to no row; "
                                  "it will not run", p.name);
        }
    }

    void Plant::register_tasks() {
        for (uint8_t i = 0; i < PROGRAM_COUNT; i++) {
            if (!_tasks->add(*this, programs[i].task, programs[i].name,
                             programs[i].period_ms, Tasks::PHASE_CONTROL))
                continue;
            // The verdict this entry reports. An ST program is not a
            // Plc::Program, so the task keeps a POINTER to where its health
            // lives rather than the object: the TASKS page asks the entry, not
            // the kind of thing behind it.
            _tasks->last().health = &_slot[i].health;
        }
    }

    void Plant::scan(uint8_t i, uint64_t now_ms) {
        if (i >= PROGRAM_COUNT) return;
        // A program that could not be wired never runs: its references are
        // inert sinks, so every value it produced would be a zero nothing
        // could tell from a reading.
        if (_slot[i].health != Health::HEALTHY) return;

        _now_ms = now_ms;
        // The CLOCK, before any body reads it. Each block differences it
        // against its own last reading, so one row serves every period.
        if (_now_ref.index != Vars::VAR_NONE)
            _io.var(_now_ref).set_i((int64_t)(uint32_t)now_ms);
        // ...and the DATE beside it. Below the floor the RTC has never been
        // set, so there is no date and 0 is what says so - a schedule counts
        // in these and refuses to run against zero.
        if (_day_ref.index != Vars::VAR_NONE) {
            const uint64_t wall = HAL::Clock::wall_ms();
            _io.var(_day_ref).set_i(
                (wall < 1600000000000ull) ? 0 : (int64_t)(wall / 86400000ull));
        }

        // DEFENCE IN DEPTH, at both ends. Before RUN the field has not
        // answered and every row still reads its DECLARED value; after RUN the
        // unit is on its way out and a law that keeps deciding is deciding
        // about a plant nobody is going to act on.
        //
        // The whole BODY is held, not each program's inputs, because every
        // stateful block seeds on its first call - a filter adopts the reading
        // it starts on, a timer datums its clock - so a held program starts
        // clean rather than climbing out of a fiction.
        if (_state != nullptr && *_state != Scheduler::RunState::RUN) return;

        _io.begin_writer(i);
        programs[i].run();
        _io.end_writer();
    }

    void Plant::scan(uint64_t now_ms) {
        for (uint8_t i = 0; i < PROGRAM_COUNT; i++) scan(i, now_ms);
    }

    Health Plant::health(uint8_t i) const {
        return (i < PROGRAM_COUNT) ? _slot[i].health : Health::INVALID;
    }

}}}  // namespace Botanik::Plc::Runtime

#endif // BOTANIK_FEATURE_PLC
