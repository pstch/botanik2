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

    PerfMonitor::PerfMonitor(uint32_t period_ms) : _period_ms(period_ms) {
        _stack_depth = HAL::get_stack_depth();
        _stack_base  = HAL::get_stack_base();
        _cpu_load.configure(ema_alpha(Config::CPU_LOAD_EMA_ATTACK_MS),
                            ema_alpha(Config::CPU_LOAD_EMA_DECAY_MS));
        _stack_usage.configure(ema_alpha(Config::STACK_USAGE_EMA_ATTACK_MS),
                               ema_alpha(Config::STACK_USAGE_EMA_DECAY_MS));
        _cpu_loop_us_window.configure(LOAD_METRIC_WINDOW);
        _stack_bytes_window.configure(LOAD_METRIC_WINDOW);
        _snap.stack_depth = _stack_depth;
        reset();
    }

    void PerfMonitor::work_begin() {
        _work_start_us = HAL::uptime_us();
        _measuring     = true;
    }

    void PerfMonitor::sample() {
        if (!_measuring) return;
        _measuring = false;

        // CPU: work duration in microseconds for sub-ms precision.
        uint64_t now_us = HAL::uptime_us();
        _last_busy_us = (now_us >= _work_start_us)
                      ? static_cast<uint32_t>(now_us - _work_start_us) : 0;
        // Instantaneous load; left uncapped so a tick whose work exceeds the period
        // reports >100% (an overrun), surfaced in the metrics rather than clamped.
        float instant = (_period_ms > 0)
                      ? static_cast<float>(_last_busy_us)
                        / static_cast<float>(_period_ms * 1000u)
                      : 0.0f;
        if (_last_busy_us > _cpu_loop_us_max) _cpu_loop_us_max = _last_busy_us;
        _cpu_loop_us_window.push(_last_busy_us);
        _cpu_load.push(instant);

        // Stack: high-water mark of the painted region.
        _measure_stack_usage();

        _snap.cpu_load_ema       = _cpu_load.value();
        _snap.cpu_loop_us_max    = _cpu_loop_us_max;
        _snap.cpu_loop_us_recent = _cpu_loop_us_window.value();
    }

    void PerfMonitor::_measure_stack_usage() {
        if (_stack_base == 0) return;
        const uint8_t* stack_base = reinterpret_cast<const uint8_t*>(_stack_base);
        // Offsets are 1-based so *(stack_base - mid) stays inside the allocated region
        // (stack_base itself is one past the top of the stack).
        uint32_t low  = 1;
        uint32_t high = (_stack_depth < Config::STACK_SCAN_LIMIT)
                      ? _stack_depth : Config::STACK_SCAN_LIMIT;
        // Binary search for the boundary between used and still-painted memory.
        while (low < high) {
            uint32_t mid = low + (high - low) / 2;
            if (*(stack_base - mid) == Config::STACK_PAINT_BYTE)    high = mid;
            else                                                    low  = mid + 1;
        }
        if (low > _stack_bytes_max) _stack_bytes_max = low;
        _stack_bytes_window.push(low);
        float ratio = (_stack_depth > 0)
                    ? static_cast<float>(low) / static_cast<float>(_stack_depth)
                    : 0.0f;
        _stack_usage.push(ratio);

        _snap.stack_usage_ema    = _stack_usage.value();
        _snap.stack_bytes_max    = _stack_bytes_max;
        _snap.stack_bytes_recent = _stack_bytes_window.value();
    }

    void PerfMonitor::_paint_below(uint8_t* caller_sp) {
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

    void PerfMonitor::reset() {
        _measuring    = false;
        _last_busy_us = 0;
        _cpu_loop_us_max = 0;
        _cpu_load.reset();
        _cpu_loop_us_window.reset();
        _stack_bytes_max = 0;
        _stack_usage.reset();
        _stack_bytes_window.reset();
        // Keep the constant stack_depth; clear the rest of the published snapshot.
        uint32_t depth = _stack_depth;
        _snap = PerfSnapshot{};
        _snap.stack_depth = depth;
    }

}}
