/**
 * @file perf.cpp
 * @brief PerfMonitor implementation — work-duration timing, stack painting and
 *        high-water scan, and the EMA / running-max / windowed-max filters.
 */
#include <cstddef>
#include <cstring>

#include "libbotanik_config.h"

#include "libbotanik/utils/perf.h"
#include "libbotanik/hal/platform.h"

namespace Botanik { namespace Utils {

    // Lifecycle
    // ------------------------------------------------------------------------

    PerfMonitor::PerfMonitor(uint32_t period_ms) : _period_ms(period_ms) {
        // measure stack base and depth
        _stack_base  = HAL::get_stack_base();
        _stack_depth = HAL::get_stack_depth();
        _snap.stack_depth = _stack_depth;

        // configure EMA filters
        _cpu_load.configure(ema_alpha(Config::CPU_LOAD_EMA_ATTACK_MS),
                            ema_alpha(Config::CPU_LOAD_EMA_DECAY_MS));
        _jitter.configure(ema_alpha(Config::CPU_JITTER_EMA_ATTACK_MS),
                          ema_alpha(Config::CPU_JITTER_EMA_DECAY_MS));
        _stack_usage.configure(ema_alpha(Config::STACK_USAGE_EMA_ATTACK_MS, STACK_SLICE_MS),
                               ema_alpha(Config::STACK_USAGE_EMA_DECAY_MS,  STACK_SLICE_MS));

        // configure rolling-max windows
        _cpu_loop_us_window.configure(LOAD_METRIC_WINDOW);
        _jitter_window.configure(LOAD_METRIC_WINDOW);

        // reset monitoring state variables
        reset();
    }

    void PerfMonitor::reset() {
        _measuring    = false;

        // reset CPU load monitoring state
        _last_busy_us = 0;
        _cpu_loop_us_max = 0;
        _cpu_load.reset();
        _cpu_loop_us_window.reset();

        // reset CPU jitter monitoring state
        _jitter.reset();
        _jitter_window.reset();
        _jitter_us_max = 0;

        // reset stack monitoring state
        _stack_bytes_max = 0;
        _stack_usage.reset();
        for (uint32_t i = 0; i < Config::LOAD_METRIC_BUCKETS; ++i) _stack_slices[i] = 0;
        _stack_head       = 0;
        _stack_slice_tick = 0;

        // force a fresh paint before the next scan; the old paint is now stale
        _repaint_pending  = true;

        // clear current snapshot
        uint32_t depth = _stack_depth;
        _snap = PerfSnapshot{};
        _snap.stack_depth = depth;
    }

    // Per-loop measurement
    // ------------------------------------------------------------------------

    // open the monitoring window
    void PerfMonitor::work_begin() {
        _work_start_us = HAL::uptime_us();
        _measuring     = true;
    }

    // close the monitoring window and sample metrics
    void PerfMonitor::sample() {
        // only sample a window we actually opened, and consume it so it can't be sampled twice
        if (!_measuring) return; _measuring = false;

        // measure busy time and instantaneous CPU load 
        uint64_t now_us = HAL::uptime_us();
        _last_busy_us = (now_us >= _work_start_us)
                      ? static_cast<uint32_t>(now_us - _work_start_us) : 0;
        float instant = (_period_ms > 0) ? static_cast<float>(_last_busy_us)
                        / static_cast<float>(_period_ms * 1000u) : 0.0f;
        // update peak busy time value
        if (_last_busy_us > _cpu_loop_us_max) _cpu_loop_us_max = _last_busy_us;
        // push busy time into the rolling-max window
        _cpu_loop_us_window.push(_last_busy_us);
        // push CPU load into the rolling-max window
        _cpu_load.push(instant);

        // measure stack usage if we are at a stack slice boundary
        if (++_stack_slice_tick >= STACK_SLICE_TICKS) {
            _stack_slice_tick = 0;
            _measure_stack_usage();
            _repaint_pending = true;
        }

        // update snapshot values
        _snap.cpu_load_ema       = _cpu_load.value();
        _snap.cpu_loop_us_max    = _cpu_loop_us_max;
        _snap.cpu_loop_us_recent = _cpu_loop_us_window.value();
    }

    void PerfMonitor::record_jitter(uint32_t jitter_us) {
        // measure instantaneous jitter as a fraction of the tick interval
        float instant = (_period_ms > 0) ? static_cast<float>(jitter_us)
                        / static_cast<float>(_period_ms * 1000u) : 0.0f;
        // update peak jitter time value
        if (jitter_us > _jitter_us_max) _jitter_us_max = jitter_us;
        // push jitter time into the rolling-max window
        _jitter_window.push(jitter_us);
        // push jitter fraction into the rolling-max window
        _jitter.push(instant);

        // update snapshot values
        _snap.cpu_jitter_ema       = _jitter.value();
        _snap.cpu_jitter_us_recent = _jitter_window.value();
        _snap.cpu_jitter_us_max    = _jitter_us_max;
    }

    // Internals
    // ------------------------------------------------------------------------

    void PerfMonitor::_measure_stack_usage() {
        if (_stack_base == 0) return;
        const uint8_t* stack_base = reinterpret_cast<const uint8_t*>(_stack_base);

        // offsets are 1-based so *(stack_base - mid) stays inside the allocated region
        // (stack_base itself is one past the top of the stack)
        uint32_t low  = 1;
        uint32_t high = (_stack_depth < Config::STACK_SCAN_LIMIT)
                      ? _stack_depth : Config::STACK_SCAN_LIMIT;

        // binary search for the boundary between used and still-painted memory
        while (low < high) {
            uint32_t mid = low + (high - low) / 2;
            if (*(stack_base - mid) == Config::STACK_PAINT_BYTE)    high = mid;
            else                                                    low  = mid + 1;
        }

        // measure stack usage as a fraction of the stack depth
        float ratio = (_stack_depth > 0) ? static_cast<float>(low) 
                      / static_cast<float>(_stack_depth) : 0.0f;
        // push stack usage fraction into the rolling-max window
        _stack_usage.push(ratio);
        // update the peak stack usage value
        if (low > _stack_bytes_max) _stack_bytes_max = low;
        // update current slice of stack usage 
        _stack_slices[_stack_head] = low;
        _stack_head = (_stack_head + 1) % Config::LOAD_METRIC_BUCKETS;

        // measure recent maximum of stack usage
        uint32_t recent = 0;
        for (uint32_t i = 0; i < Config::LOAD_METRIC_BUCKETS; ++i)
            if (_stack_slices[i] > recent) recent = _stack_slices[i];

        // update snapshot values
        _snap.stack_usage_ema    = _stack_usage.value();
        _snap.stack_bytes_max    = _stack_bytes_max;
        _snap.stack_bytes_recent = recent;
    }

    void PerfMonitor::_paint_below(uint8_t* caller_sp) {
        if (_stack_base == 0) return;
        uint8_t* abs_bottom = reinterpret_cast<uint8_t*>(_stack_base - _stack_depth);
        // Margin must cover this function's frame + memset's frame so neither is
        // clobbered mid-paint. 256 B is well above what either uses on x86_64 or
        // ARM Cortex-M; the painted ceiling sits just below the caller's SP.
        constexpr ptrdiff_t MARGIN = 256;
        // bail out if the current stack pointer is below our margin
        if (caller_sp < abs_bottom + MARGIN) return;
        uint8_t* paint_top = caller_sp - MARGIN;
        // Cap paint region to STACK_SCAN_LIMIT bytes to avoid doing memset() on 
        // the entire thread stack allocation on every tick.
        uint8_t* paint_bot = paint_top - Config::STACK_SCAN_LIMIT;
        // clamp paint bottom
        if (paint_bot < abs_bottom) paint_bot = abs_bottom;
        // if we have a valid region, memset() it to STACK_PAINT_BYTE
        if (paint_top > paint_bot)
            memset(paint_bot, Config::STACK_PAINT_BYTE, paint_top - paint_bot);
    }

}}
