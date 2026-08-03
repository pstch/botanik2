/**
 * @file scan.cpp
 * @brief Scan driver: decide which tasks are due and run them (see scan.h).
 */

#include "scan.h"

#include "../hal/platform.h"

namespace Botanik { namespace Scan {

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
