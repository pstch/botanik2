/**
 * @file scheduler.cpp
 * @brief LoopScheduler implementation — tick timing and overrun reporting. All
 *        performance instrumentation lives in PerfMonitor (utils/perf).
 */
#ifdef TARGET_SIMULATOR
#include <cerrno>
#include <cstring>
#include <sched.h>
#include <sys/mman.h>
#endif

#include "libbotanik_config.h"

#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/plcio/vars.h"

namespace Botanik { namespace Scheduler {

    LoopScheduler::LoopScheduler(uint32_t period_ms, Logs::Logger* logger)
        : _period_ms(period_ms), _perf(period_ms), _logger(logger) {
            reset();
        }

    void LoopScheduler::_log_startup() {
        if (_logger != nullptr)
            _logger->info(PlcIO::BOOT, "scheduler started, period=%ums", (unsigned)_period_ms);
    }

    void LoopScheduler::_skip_beats(uint64_t now) {
        if (now >= _next_tick_ms + _period_ms) {
            uint64_t delay = now - _next_tick_ms;
            uint64_t missed_beats = delay / _period_ms;
            _next_tick_ms += (missed_beats * _period_ms);
            _overrun_count += (uint32_t)missed_beats;
            _context.overruns = _overrun_count;
            // Throttle the warning: accumulate skipped beats and emit at most once
            // per SCHEDULER_OVERRUN_LOG_INTERVAL_MS, reporting the aggregate so a
            // burst of overruns doesn't itself flood the log (and the TX path).
            _overrun_pending += (uint32_t)missed_beats;
            if (_logger != nullptr &&
                now - _overrun_log_last_ms >= Config::SCHEDULER_OVERRUN_LOG_INTERVAL_MS) {
                _logger->warning(PlcIO::STATUS, "scheduler overrun: skipped %lu beat(s)",
                    (unsigned long)_overrun_pending);
                _overrun_log_last_ms = now;
                _overrun_pending = 0;
            }
        }
    }

    void LoopScheduler::reset() {
        _next_tick_ms = HAL::uptime_ms() + _period_ms;
        _perf.reset();
        _overrun_log_last_ms = 0;  // log the first overrun after a reset immediately
        _overrun_pending = 0;
    }

    bool LoopScheduler::tick() {
        if (!_started) { _started = true; _log_startup(); }  // announce on first tick
        uint64_t now_us = HAL::uptime_us();     // measure current time (us, for jitter)
        uint64_t now    = now_us / 1000;
        _perf.sample();                         // measure past work + stack usage
        if (now < _next_tick_ms) {               // yield until next tick
            // Sleep ~half the remaining gap to absorb scheduling jitter,
            // (degrades to a busy loop under 2 ms)
            HAL::sleep_ms((uint32_t)((_next_tick_ms - now) / 2));
            return false;
        }
        _context.perf = _perf.snapshot();
        _perf.work_begin();
        _skip_beats(now);                       // skip missed beats (catch-up)
        // Sub-period lateness against the (post-catch-up) grid point this tick belongs
        // to; whole skipped beats are accounted separately as overruns.
        _perf.record_jitter((uint32_t)(now_us - _next_tick_ms * 1000));
        _next_tick_ms += _period_ms;            // advance time grid
        ++_tick_count;
        _context.tick_count = _tick_count;
        // Caller must invoke perf().paint_stack() at user-code depth before working.
        return true;                            // return true so that work can start
    }

#ifdef TARGET_SIMULATOR
    void make_process_realtime(int cpu_core_id, Logs::Logger* logger) {
        // 1. Lock memory pages into RAM to avoid page fault latency
        if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1 && logger != nullptr)
            logger->warning(PlcIO::BOOT, "mlockall failed: \n%s", strerror(errno));

        // 2. Set the real-time scheduling policy (SCHED_FIFO) and priority
        struct sched_param param;
        param.sched_priority = 95; // Priorities range from 1 (low) to 99 (high)
        if (sched_setscheduler(0, SCHED_FIFO, &param) == -1 && logger != nullptr)
            logger->warning(PlcIO::BOOT, "sched_setscheduler failed: \n%s", strerror(errno));

        // 3. Pin the process to a dedicated CPU core to prevent task migration
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(cpu_core_id, &mask);
        if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) == -1 && logger != nullptr)
            logger->warning(PlcIO::BOOT, "sched_setaffinity failed: \n%s", strerror(errno));
    }
#endif

} }
