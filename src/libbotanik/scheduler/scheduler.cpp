// TODO: AnsiTerminal still reads the concrete SchedulerContext/PlcSyncContext;
// a future unified Context wrapper could decouple it from both. See TODO.md.
/**
 * @file scheduler.cpp
 * @brief LoopScheduler implementation — tick timing and overrun reporting. All
 *        performance instrumentation lives in PerfMonitor (utils/perf), which the
 *        scheduler drives and whose snapshot it publishes into the SchedulerContext.
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

    // Lifecycle
    // ------------------------------------------------------------------------

    LoopScheduler::LoopScheduler(uint32_t period_ms, Logs::Logger* logger)
        : _period_ms(period_ms), _perf(period_ms), _logger(logger) {
            reset();
        }

    void LoopScheduler::reset() {
        _next_tick_ms = HAL::uptime_ms() + _period_ms;
        _perf.reset();
        // log the first overrun after a reset immediately
        _overrun_log_last_ms = 0;
        _overrun_pending = 0;
    }

    // Tick
    // ------------------------------------------------------------------------

    bool LoopScheduler::tick() {
        // log startup message on first tick
        if (!_started) { _started = true; _log_startup(); }  
        // measure current time (us, for jitter)
        uint64_t now_us = HAL::uptime_us();
        uint64_t now_ms = HAL::uptime_ms();
        int64_t remaining_ms = _next_tick_ms - now_ms;
        // measure past work + stack usage
        _perf.sample();
        // yield/sleep until next tick
        if (remaining_ms > 0) {
            // sleep until grid point minus one millisecond
            if (remaining_ms > 1) HAL::sleep_ms((uint32_t)(remaining_ms - 1));
            // return false so that work can be deferred
            // NOTE: this implements a busy loop started 1 ms before the grid point
            return false;
        }
        // publish the perf snapshot into the context (PerfSnapshot base subobject)
        static_cast<Utils::PerfSnapshot&>(_context) = _perf.snapshot();
        // open work window
        _perf.work_begin();
        // skip missed beats (and log overruns)
        _skip_beats(now_ms);
        // measure lateness against time grid
        _perf.record_jitter((uint32_t)(now_us - _next_tick_ms * 1000));
        // advance time grid
        _next_tick_ms += _period_ms;            // advance time grid
        // increment tick count
        ++_context.tick_count;
        // return true so that work can start
        return true;                            // return true so that work can start
    }

    // Internals
    // ------------------------------------------------------------------------

    void LoopScheduler::_skip_beats(uint64_t now) {
        if (now >= _next_tick_ms + _period_ms) {
            // count overruns
            uint64_t delay = now - _next_tick_ms;
            uint64_t missed_beats = delay / _period_ms;
            _next_tick_ms += (missed_beats * _period_ms);
            _context.overruns += (uint32_t)missed_beats;
            // report the aggregate overrun count to avoid flooding the log
            _overrun_pending += (uint32_t)missed_beats;
            if (_logger != nullptr &&
                now - _overrun_log_last_ms >= Config::SCHEDULER_XRUN_LOG_MS) {
                _logger->warning(PlcIO::STATUS, "scheduler overrun: skipped %lu beat(s)",
                    (unsigned long)_overrun_pending);
                _overrun_log_last_ms = now;
                _overrun_pending = 0;
            }
        }
    }

    void LoopScheduler::_log_startup() {
        if (_logger != nullptr)
            _logger->info(PlcIO::BOOT, "scheduler started, period=%ums", (unsigned)_period_ms);
    }

#ifdef TARGET_SIMULATOR

    // Simulator utilities (memlock, CPU-pinning and SCHED_FIFO)
    // ------------------------------------------------------------------------

    void make_process_realtime(int cpu_core_id, Logs::Logger* logger) {
        // lock memory pages into RAM to avoid page fault latency
        if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1 && logger != nullptr)
            logger->warning(PlcIO::BOOT, "mlockall failed: \n%s", strerror(errno));

        // set the real-time scheduling policy (SCHED_FIFO) and priority
        struct sched_param param;
        param.sched_priority = 95; // Priorities range from 1 (low) to 99 (high)
        if (sched_setscheduler(0, SCHED_FIFO, &param) == -1 && logger != nullptr)
            logger->warning(PlcIO::BOOT, "sched_setscheduler failed: \n%s", strerror(errno));

        // pin the process to a dedicated CPU core to prevent task migration
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(cpu_core_id, &mask);
        if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) == -1 && logger != nullptr)
            logger->warning(PlcIO::BOOT, "sched_setaffinity failed: \n%s", strerror(errno));
    }
    
#endif

} }
