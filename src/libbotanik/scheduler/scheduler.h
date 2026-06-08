#ifndef SCHEDULER_SCHEDULER_H
#define SCHEDULER_SCHEDULER_H

/**
 * @file scheduler.h
 * @brief LoopScheduler — non-blocking fixed-period loop driver with CPU-load and
 *        stack-usage metrics (EMA-smoothed) and overrun handling.
 */

#include <cstdint>
#include <cstring>

#include "libbotanik_config.h"

// Stack-usage measurement (paint + scan) is fundamentally incompatible with
// AddressSanitizer: with detect_stack_use_after_return, locals move to a
// heap-backed "fake stack", so the paint and the scan touch different memory and
// the binary search walks to its cap (STACK_SCAN_LIMIT). Tagging the functions
// that paint/scan the raw stack with no_sanitize("address") keeps their allocas
// (incl. paint_stack's sp_marker) on the real thread stack — so paint and scan
// agree — and stops the downward scan from tripping a redzone false positive.
// Expands to nothing in non-sanitized builds.

#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define __BOTANIK_NO_ASAN__ __attribute__((no_sanitize("address")))
#  endif
#elif defined(__SANITIZE_ADDRESS__)
#  define __BOTANIK_NO_ASAN__ __attribute__((no_sanitize("address")))
#endif
#ifndef __BOTANIK_NO_ASAN__
#  define __BOTANIK_NO_ASAN__
#endif

namespace Botanik { namespace Logs { class Logger; } }

namespace Botanik { namespace Scheduler {

    /** @brief Per-tick EMA smoothing factor for a given response time constant
     *  (ms). Derived from the tick period so the filter's wall-clock response
     *  stays fixed regardless of TASK_PERIOD_MS (forward-Euler: alpha = dt/tau).
     *  A tau at or below one tick collapses to 1.0 (instant, since the filter
     *  can't resolve anything faster than a single tick). */
    constexpr float ema_alpha(uint32_t tau_ms) {
        return tau_ms <= Config::TASK_PERIOD_MS
            ? 1.0f
            : (float)Config::TASK_PERIOD_MS / (float)tau_ms;
    }

    /** @brief Number of per-tick samples spanning LOAD_METRIC_WINDOW_MS. The metric
     *  ring buffers are sized from this; one sample is recorded per tick. */
    static constexpr uint32_t LOAD_METRIC_WINDOW = Config::LOAD_METRIC_WINDOW_MS / Config::TASK_PERIOD_MS;

    // Per-tick smoothing factors derived from the configured response time constants.
    static constexpr float CPU_LOAD_EMA_ATTACK    = ema_alpha(Config::CPU_LOAD_EMA_ATTACK_MS);
    static constexpr float CPU_LOAD_EMA_DECAY     = ema_alpha(Config::CPU_LOAD_EMA_DECAY_MS);
    static constexpr float STACK_USAGE_EMA_ATTACK = ema_alpha(Config::STACK_USAGE_EMA_ATTACK_MS);
    static constexpr float STACK_USAGE_EMA_DECAY  = ema_alpha(Config::STACK_USAGE_EMA_DECAY_MS);

    /** @brief Stable metrics snapshot consumed by AnsiTerminal for the status bar. */
    struct SchedulerContext {
        float    cpu_load        = 0.0f;   // peak over LOAD_METRIC_WINDOW ticks, fraction 0..1
        float    cpu_load_ema    = 0.0f;
        uint32_t stack_usage     = 0;      // peak over LOAD_METRIC_WINDOW ticks, bytes
        float    stack_usage_ema = 0.0f;   // fraction 0..1
        uint32_t stack_depth     = 0;      // total thread stack allocation in bytes
        uint32_t tick_count      = 0;
        uint32_t overruns        = 0;      // cumulative beats skipped due to overruns
    };

    class LoopScheduler {
    private:
        // Configuration & state
        uint32_t    _period_ms              = Config::TASK_PERIOD_MS;
        uint64_t    _next_tick_ms           = 0;
        // Timing state to measure the user's external code block
        bool        _is_working             = false;
        uint64_t    _work_start_us          = 0;
        // CPU usage metrics
        uint32_t    _last_busy_us           = 0;
        float       _cpu_load_window[LOAD_METRIC_WINDOW] = {};
        uint32_t    _cpu_load_window_idx    = 0;
        float       _cpu_load               = 0.0f;  // peak over LOAD_METRIC_WINDOW ticks
        float       _cpu_load_ema           = 0.0f;
        // Memory usage metrics
        uint32_t    _stack_depth            = 0;
        uintptr_t   _stack_base             = 0;
        float       _stack_usage_window[LOAD_METRIC_WINDOW] = {};
        uint32_t    _stack_usage_window_idx = 0;
        uint32_t    _stack_usage            = 0;   // peak over LOAD_METRIC_WINDOW ticks, bytes
        float       _stack_usage_ema        = 0;
        Logs::Logger*   _logger        = nullptr;
        uint32_t        _tick_count    = 0;
        uint32_t        _overrun_count = 0;
        uint64_t        _overrun_log_last_ms = 0;  // timestamp of the last overrun warning
        uint32_t        _overrun_pending     = 0;  // beats skipped since that warning
        bool            _started       = false;  // startup message emitted on first tick()
        SchedulerContext _context      = {};

        /** @brief Record elapsed work time and update CPU load metrics. */
        void _measure_work();
        /** @brief Sample the current stack pointer and update stack usage metrics. */
        __BOTANIK_NO_ASAN__ void _measure_stack_usage();
        /** @brief Paint the free stack region (below caller_sp) with STACK_PAINT_BYTE. */
        __BOTANIK_NO_ASAN__ void _paint_below(uint8_t* caller_sp);
        /** @brief Advance the next-tick anchor to skip missed beats when the loop falls behind. */
        void _skip_beats(uint64_t now);
        /** @brief Emit the "scheduler started" log entry (called once, on the first tick()). */
        void _log_startup();

    public:
        /**
         * @brief Construct a new non-blocking Loop Scheduler
         * @param period_ms The target period of the loop in milliseconds.
         * @param logger    Optional logger for startup and periodic load reports.
         */
        LoopScheduler(uint32_t period_ms = Config::TASK_PERIOD_MS, Logs::Logger* logger = nullptr);

        /**
         * @brief Resets the scheduler anchor point to the current platform uptime.
         */
        void reset();

        /**
         * @brief Non-blocking check. Evaluates if it is time to run the task.
         * @return true exactly ONCE per period when it is time to execute.
         */
        bool tick();

        /**
         * @brief Paint the free stack below the caller's SP with STACK_PAINT_BYTE.
         *
         * Must be inlined into the caller's frame so &sp_marker reflects the caller's
         * SP, not this method's. Called once per tick from the main loop after tick()
         * returns true; subsequent step() work then dirties the painted region in
         * proportion to its actual depth.
         */
        __BOTANIK_NO_ASAN__ inline void paint_stack() {
            volatile uint8_t sp_marker = 0;
            _paint_below(const_cast<uint8_t*>(&sp_marker));
        }

        /** @brief Stable metrics snapshot for external consumers (e.g. status bar). */
        SchedulerContext* context() { return &_context; }

    };

} }

#endif // SCHEDULER_SCHEDULER_H