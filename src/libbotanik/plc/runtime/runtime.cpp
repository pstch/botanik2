/**
 * @file runtime.cpp
 * @brief HAND-WRITTEN - the join between the beat and the generated ST.
 */

#include "libbotanik/plc/runtime/runtime.h"

#include <cstdio>
#include "libbotanik/plc/runtime/bank.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/utils/assert.h"

namespace Botanik { namespace Plc { namespace Runtime {

    
    void Plant::on_setup(uint64_t now_ms) {
        (void)now_ms;
        wire();
    }

    void Plant::wire() {
        io = &_io;
        _now_ref = _io.find("sys_now_ms");
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
        for (uint8_t i = 0; i < PROGRAM_COUNT; i++)
            _tasks->add(*this, programs[i].task, programs[i].name,
                        programs[i].period_ms, Tasks::PHASE_CONTROL);
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
