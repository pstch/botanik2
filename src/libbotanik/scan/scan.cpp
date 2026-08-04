/**
 * @file scan.cpp
 * @brief Scan driver: decide which tasks are due and run them (see scan.h).
 */

#include "scan.h"

#include "../../libbotanik_config.h"
#include "../hal/platform.h"

namespace Botanik { namespace Scan {

    void Driver::setup(uint64_t now_ms) {
        (void)now_ms;
        if (_tasks == nullptr || _stats == nullptr) return;
        if (Config::SCAN_STAGGER_MS == 0) return;

        for (uint8_t i = 0; i < _count; i++) {
            // An every-beat program has no interval to shift inside, and the
            // modulo would divide by zero.
            if (_tasks[i].period_ms == 0) continue;
            // Wrapped into the period: an offset at or past it would skip a whole
            // activation rather than shifting one, and with enough programs the
            // raw i*step runs past the shorter periods.
            _stats[i].set_offset((uint32_t)i * Config::SCAN_STAGGER_MS
                                 % _tasks[i].period_ms);
        }
    }

    void Driver::on_step(uint64_t now_ms) {
        if (_tasks == nullptr || _stats == nullptr || _io == nullptr) return;

        for (uint8_t i = 0; i < _count; i++) {
            const Task& t = _tasks[i];
            TaskStats&  s = _stats[i];
            if (t.run == nullptr) continue;

            // Due, how late, and how many activations were dropped: one rule,
            // shared with every other multi-rate caller (scheduler/task_timer.h).
            uint32_t dt     = 0;
            uint16_t missed = 0;
            if (!s.due(now_ms, t.period_ms, dt, missed)) continue;

            ScanContext ctx = { now_ms, dt, missed, *_io };

            t.run(ctx);

            s.ran(now_ms);
        }
    }

}}  // namespace Botanik::Scan
