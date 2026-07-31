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

            uint32_t dt     = 0;
            uint16_t missed = 0;

            if (s.runs == 0) {
                // First activation: run now, and report the nominal period as
                // dt. Handing a timer "everything since boot" on its first scan
                // would fire it immediately.
                dt = t.period_ms;
            } else {
                uint64_t elapsed = now_ms - s.last_run_ms;
                if (t.period_ms != 0 && elapsed < t.period_ms) continue;

                // Run once however late we are: dropped activations are counted,
                // never caught up (see scan.h). dt is the REAL elapsed time, so a
                // task that ran late does not lose the time it was late by.
                dt = (elapsed > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)elapsed;
                if (t.period_ms != 0 && elapsed >= 2ull * t.period_ms) {
                    uint64_t skipped = (elapsed / t.period_ms) - 1;
                    missed = (skipped > 0xFFFF) ? 0xFFFF : (uint16_t)skipped;
                }
            }

            ScanContext ctx = { now_ms, dt, missed, *_io };

            t.run(ctx);

            s.last_run_ms = now_ms;
            s.runs++;
        }
    }

}}  // namespace Botanik::Scan
