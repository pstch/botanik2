/**
 * @file scheduler.cpp
 * @brief LoopScheduler implementation — tick timing, work/stack measurement,
 *        metric EMAs, stack painting, and overrun reporting.
 */
#include <cstddef>
#ifdef TARGET_SIMULATOR
#include <unistd.h>
#endif

#include "libbotanik_config.h"

#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/plcio/vars.h"

namespace Botanik { namespace Scheduler {

    LoopScheduler::LoopScheduler(uint32_t period_ms, Logs::Logger* logger)
        : _period_ms(period_ms), _logger(logger) {
            _stack_depth = HAL::get_stack_depth();
            _stack_base = HAL::get_stack_base();
            _context.stack_depth = _stack_depth;
            reset();
        }

    void LoopScheduler::_log_startup() {
        if (_logger != nullptr)
            _logger->info(PlcIO::BOOT, "scheduler started, period=%ums", (unsigned)_period_ms);
    }

    void LoopScheduler::_measure_work() {
        if (_is_working) {
            // calculate last work duration in microseconds for sub-ms precision
            uint64_t now_us = HAL::uptime_us();
            if (now_us >= _work_start_us) {
                _last_busy_us = static_cast<uint32_t>(now_us - _work_start_us);
            } else {
                _last_busy_us = 0;
            }
            // compute instantaneous CPU load; left uncapped so a tick whose work
            // exceeds the period reports >100% (an overrun), surfaced in the peak
            // and EMA metrics rather than being clamped away.
            float instant = 0.0f;
            if (_period_ms > 0) {
                instant = static_cast<float>(_last_busy_us)
                        / static_cast<float>(_period_ms * 1000u);
            }
            // store in circular window and compute peak
            _cpu_load_window[_cpu_load_window_idx] = instant;
            _cpu_load_window_idx = (_cpu_load_window_idx + 1) % LOAD_METRIC_WINDOW;
            float peak = 0.0f;
            for (uint32_t i = 0; i < LOAD_METRIC_WINDOW; ++i)
                if (_cpu_load_window[i] > peak) peak = _cpu_load_window[i];
            _cpu_load = peak;
            // apply asymmetric EMA filter to instantaneous load
            float alpha = (instant > _cpu_load_ema) ?
                CPU_LOAD_EMA_ATTACK : CPU_LOAD_EMA_DECAY;
            _cpu_load_ema = (alpha * instant) + ((1.0f - alpha) * _cpu_load_ema);
            _context.cpu_load     = _cpu_load;
            _context.cpu_load_ema = _cpu_load_ema;
            _measure_stack_usage();
            _is_working = false;
        }
    }

    void LoopScheduler::_measure_stack_usage() {
        if (_stack_base == 0) return;
        const uint8_t* stack_base = reinterpret_cast<const uint8_t*>(_stack_base);
        // Offsets are 1-based so *(stack_base - mid) stays inside the allocated region
        // (stack_base itself is one past the top of the stack).
        uint32_t low = 1;
        uint32_t high = (_stack_depth < Config::STACK_SCAN_LIMIT) ? _stack_depth : Config::STACK_SCAN_LIMIT;
        // perform binary search
        while (low < high) {
            uint32_t mid = low + (high - low) / 2;
            if (*(stack_base - mid) == Config::STACK_PAINT_BYTE)    high = mid;
            else                                                    low = mid + 1;
        }
        // store in circular window and compute peak
        float ratio = (_stack_depth > 0)
                    ? static_cast<float>(low) / static_cast<float>(_stack_depth)
                    : 0.0f;
        _stack_usage_window[_stack_usage_window_idx] = ratio;
        _stack_usage_window_idx = (_stack_usage_window_idx + 1) % LOAD_METRIC_WINDOW;
        float peak = 0.0f;
        for (uint32_t i = 0; i < LOAD_METRIC_WINDOW; ++i)
            if (_stack_usage_window[i] > peak) peak = _stack_usage_window[i];
        _stack_usage = static_cast<uint32_t>(peak * static_cast<float>(_stack_depth));
        // apply asymmetric EMA filter to instantaneous usage
        float alpha = (_stack_usage_ema < ratio) ?
            STACK_USAGE_EMA_ATTACK : STACK_USAGE_EMA_DECAY;
        _stack_usage_ema = (alpha * ratio) + ((1.0f - alpha) * _stack_usage_ema);
        _context.stack_usage     = _stack_usage;
        _context.stack_usage_ema = _stack_usage_ema;
    }

    void LoopScheduler::_paint_below(uint8_t* caller_sp) {
        if (_stack_base == 0) return;
        uint8_t* abs_bottom = reinterpret_cast<uint8_t*>(_stack_base - _stack_depth);
        // Margin must cover this function's frame + memset's frame so neither is
        // clobbered mid-paint. 256 B is well above what either uses on x86_64 or
        // ARM Cortex-M; the painted ceiling sits just below the caller's SP.
        constexpr ptrdiff_t MARGIN = 256;
        if (caller_sp < abs_bottom + MARGIN) return;
        uint8_t* paint_top = caller_sp - MARGIN;
        // Cap paint region to STACK_SCAN_LIMIT bytes to avoid memset-ing the entire
        // (Linux: 8 MB) thread stack allocation on every tick.
        uint8_t* paint_bot = paint_top - Config::STACK_SCAN_LIMIT;
        if (paint_bot < abs_bottom) paint_bot = abs_bottom;
        if (paint_top > paint_bot)
            memset(paint_bot, Config::STACK_PAINT_BYTE, paint_top - paint_bot);
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
        _is_working = false;
        _last_busy_us = 0;
        memset(_cpu_load_window, 0, sizeof(_cpu_load_window));
        _cpu_load_window_idx = 0;
        _cpu_load = 0.0f;
        _cpu_load_ema = 0.0f;
        memset(_stack_usage_window, 0, sizeof(_stack_usage_window));
        _stack_usage_window_idx = 0;
        _stack_usage = 0;
        _stack_usage_ema = 0.0f;
        _overrun_log_last_ms = 0;  // log the first overrun after a reset immediately
        _overrun_pending = 0;
    }

    bool LoopScheduler::tick() {
        if (!_started) { _started = true; _log_startup(); }  // announce on first tick
        uint64_t now = HAL::uptime_ms();        // measure current time
        _measure_work();                        // measure past work
        if (now < _next_tick_ms) {               // yield until next tick
            #ifdef TARGET_SIMULATOR
            usleep((useconds_t)((_next_tick_ms - now) * 1000));
            #endif
            return false;
        }
        _skip_beats(now);                       // skip missed beats (catch-up)
        _next_tick_ms += _period_ms;            // advance time grid
        _is_working = true;
        ++_tick_count;
        _context.tick_count = _tick_count;
        // Caller must invoke paint_stack() at user-code depth before starting work.
        _work_start_us = HAL::uptime_us();
        return true;                            // return true so that work can start
    }

} }
