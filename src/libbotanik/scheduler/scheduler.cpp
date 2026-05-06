#include "libbotanik_config.h"

#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/hal/platform.h"

namespace Botanik { namespace Scheduler {

    namespace {
        using Config::CPU_LOAD_EMA_ATTACK;
        using Config::CPU_LOAD_EMA_DECAY;
    }

    LoopScheduler::LoopScheduler(uint32_t period_ms)
        : _period_ms(period_ms) {
            _stack_depth = HAL::get_stack_depth();
            _stack_base = HAL::get_stack_base();
            reset();
        }

    void LoopScheduler::_measure_work(uint64_t now) {
        if (_is_working) {
            // calculate last work duration
            if (now >= _work_start_ms) {
                _last_busy_ms = static_cast<uint32_t>(now - _work_start_ms);
            } else {
                // safeguard in case clock ends up not being monotonic
                _last_busy_ms = 0;
            }
            // measure CPU load
            if (_period_ms > 0) {
                _cpu_load = static_cast<float>(_last_busy_ms)/_period_ms;
                if (_cpu_load > 1.0f) _cpu_load = 1.0f;
            }
            // apply asymmetric EMA filter to measured CPU usage
            float alpha = (_cpu_load > _cpu_load_ema) ?
                CPU_LOAD_EMA_ATTACK : CPU_LOAD_EMA_DECAY;
            _cpu_load_ema = (alpha * _cpu_load) + ((1.0f - alpha) * _cpu_load_ema);
            _is_working = false;
        }
    }

    void LoopScheduler::_measure_stack_usage() {
        // Retrieve the start (top origin) of the stack from HAL
        const uint8_t* stack_base = reinterpret_cast<const uint8_t*>(HAL::get_stack_base());
        if (stack_base == nullptr) return;
        uint32_t low = 0;
        uint32_t high = Config::STACK_SCAN_LIMIT;
        // perform binary search
        while (low < high) {
            uint32_t mid = low + (high - low) / 2;
            if (*(stack_base - mid) == Config::STACK_PAINT_BYTE)    high = mid;
            else                                                    low = mid + 1;
        }
        // apply asymmetric EMA filter to measured stack usage
        _stack_usage = static_cast<float>(low) / _stack_depth;
        float alpha = (_stack_usage_ema < _stack_usage) ?
            Config::STACK_USAGE_EMA_ATTACK : Config::STACK_USAGE_EMA_DECAY;
        _stack_usage_ema = (alpha * _stack_usage) + ((1.0f - alpha) * _stack_usage_ema);
    }

    void LoopScheduler::_skip_beats(uint64_t now) {
        if (now >= _next_tick_ms + _period_ms) {
            uint64_t delay = now - _next_tick_ms;
            uint64_t missed_beats = delay / _period_ms;
            _next_tick_ms += (missed_beats * _period_ms);
        }
    }

    void LoopScheduler::reset() {
        _next_tick_ms = HAL::uptime_ms() + _period_ms;
        _is_working = false;
        _last_busy_ms = 0;
        _cpu_load = 0.0f;
        _cpu_load_ema = 0.0f;
    }

    bool LoopScheduler::tick() {
        uint64_t now = HAL::uptime_ms();        // measure current time
        _measure_work(now);                     // measure past work
        if (now < _next_tick_ms) return false;  // yield early if idle
        _skip_beats(now);                       // skip missed beats (catch-up)
        _next_tick_ms += _period_ms;            // advance time grid
        _is_working = true;                     // mark the caller code as running
        _work_start_ms = now;                   // store when the caller work started
        return true;                            // return true so that work can start
    }

} }