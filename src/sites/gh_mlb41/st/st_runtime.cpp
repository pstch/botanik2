/**
 * @file st_runtime.cpp
 * @brief HAND-WRITTEN - the join between the beat and the generated ST.
 */

#include "st_runtime.h"

#include <cstdio>
#include "st_bank.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/utils/assert.h"

namespace Site {

    using namespace Botanik;

    void StRuntime::on_setup(uint64_t now_ms) {
        (void)now_ms;
        wire();
    }

    void StRuntime::wire() {
        Botanik::St::io = &_io;
        _now_ref = _io.find("sys_now_ms");
        BOTANIK_ASSERT(ST_PROGRAM_COUNT <=
                       (uint8_t)(sizeof _slot / sizeof _slot[0]));

        char row[40];
        for (uint8_t i = 0; i < ST_PROGRAM_COUNT; i++) {
            const StProgram& p = st_programs[i];

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
            _slot[i].health = wired ? Plc::Health::HEALTHY
                                    : Plc::Health::INVALID;

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

    void StRuntime::register_tasks() {
        for (uint8_t i = 0; i < ST_PROGRAM_COUNT; i++)
            _tasks->add(*this, st_programs[i].task, st_programs[i].name,
                        st_programs[i].period_ms, Tasks::PHASE_CONTROL);
    }

    void StRuntime::scan(uint8_t i, uint64_t now_ms) {
        if (i >= ST_PROGRAM_COUNT) return;
        // A program that could not be wired never runs: its references are
        // inert sinks, so every value it produced would be a zero nothing
        // could tell from a reading.
        if (_slot[i].health != Plc::Health::HEALTHY) return;

        _now_ms = now_ms;
        // The CLOCK, before any body reads it. Each block differences it
        // against its own last reading, so one row serves every period.
        if (_now_ref.index != Vars::VAR_NONE)
            _io.var(_now_ref).set_i((int64_t)(uint32_t)now_ms);

        _io.begin_writer(i);
        st_programs[i].run();
        _io.end_writer();
    }

    void StRuntime::scan(uint64_t now_ms) {
        for (uint8_t i = 0; i < ST_PROGRAM_COUNT; i++) scan(i, now_ms);
    }

    Plc::Health StRuntime::health(uint8_t i) const {
        return (i < ST_PROGRAM_COUNT) ? _slot[i].health : Plc::Health::INVALID;
    }

}  // namespace Site
