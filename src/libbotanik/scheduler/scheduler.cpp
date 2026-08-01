// REVIEW_35: TODO
// TODO: AnsiTerminal still reads the concrete SchedulerContext; a future
// unified Context wrapper could decouple it. See TODO.md.
/**
 * @file scheduler.cpp
 * @brief LoopScheduler implementation - tick timing, overrun reporting, and the
 *        out-of-line performance instrumentation (work-duration sampling, CPU
 *        duty cycle, jitter filtering, stack painting + high-water scan). The
 *        hot tick() path and the stack paint live inline in the header.
 */
#include <cstddef>
#include <cstring>
#ifdef TARGET_SIMULATOR
#include <cerrno>
#include <sched.h>
#include <sys/mman.h>
#endif

#include "../../libbotanik_config.h"

#include "scheduler.h"
#include "../hal/platform.h"
#include "../logs/logger.h"
#include "../logs/topics.h"


namespace Botanik { namespace Scheduler {

    // Lifecycle
    // ------------------------------------------------------------------------

    LoopScheduler::LoopScheduler(uint32_t period_ms, Logs::Logger* logger)
        : _period_ms(period_ms), _logger(logger) {
        _stack_base  = HAL::Stack::base();
        _stack_depth = HAL::Stack::depth();

        _cpu_load.configure(Utils::ema_alpha(Config::CPU_LOAD_EMA_ATTACK_MS),
                            Utils::ema_alpha(Config::CPU_LOAD_EMA_DECAY_MS));
        _jitter.configure(Utils::ema_alpha(Config::CPU_JITTER_EMA_ATTACK_MS),
                          Utils::ema_alpha(Config::CPU_JITTER_EMA_DECAY_MS));
        _cpu_loop_us_window.configure(Config::LOAD_METRIC_WINDOW);
        _jitter_window.configure(Config::LOAD_METRIC_WINDOW);

        // Everything else starts at its in-class initializer; only the anchor
        // and the stack fields need the runtime values.
        _next_tick_ms             = HAL::Clock::uptime_ms() + _period_ms;
        _context.stack_bytes_free = _stack_depth;   // nothing used yet
        _context.stack_depth      = _stack_depth;
    }

    void LoopScheduler::_announce() {
#ifdef DEBUG
        if (_logger != nullptr) _logger->debug(Logs::BOOT, "LoopScheduler initialized");
#endif
    }

    // Tick
    // ------------------------------------------------------------------------
    // tick() is defined inline in the header (always_inline + BOTANIK_NO_ASAN)
    // so its trailing stack paint folds into the caller's frame and captures
    // the loop body's SP. The cold helpers below stay out of line.

    // Internals
    // ------------------------------------------------------------------------

    void LoopScheduler::_skip_beats(uint64_t now) {
        _missed_this_beat = 0;
        if (now >= _next_tick_ms + _period_ms) {
            uint64_t delay = now - _next_tick_ms;
            uint64_t missed_beats = delay / _period_ms;
            // weight this tick's metric samples
            _missed_this_beat = (uint32_t)missed_beats;
            _next_tick_ms += (missed_beats * _period_ms);
            // Count overruns - surfaced by the CPU_XRUN alarm and the `stats`
            // overrun count. No log line: it would flood, especially under sim
            // time dilation.
            _context.overruns += (uint32_t)missed_beats;
        }
    }

    void LoopScheduler::_record_jitter(uint32_t jitter_us) {
        // instantaneous jitter as a fraction of the tick interval
        float instant = _fraction_of_period(jitter_us);
        if (jitter_us > _jitter_us_max) _jitter_us_max = jitter_us;
        // age by elapsed beats
        _jitter_window.push(jitter_us, 1 + _missed_this_beat);
        _jitter.push(instant);

        _context.cpu_jitter_ema       = _jitter.value();
        _context.cpu_jitter_us_recent = _jitter_window.value();
        _context.cpu_jitter_us_max    = _jitter_us_max;
    }

    void LoopScheduler::_record_load(uint64_t now_us) {
        // duty cycle: active (non-slept) time over the nominal period.
        // Normalizing by the period (not elapsed) lets an overrunning beat read
        // past 100%.
        if (_last_release_us != 0) {
            uint64_t elapsed = now_us - _last_release_us;
            uint64_t active  = (elapsed > _slept_us) ? (elapsed - _slept_us) : 0;
            float instant    = _fraction_of_period(active);
            _cpu_load.push(instant);
            _context.cpu_load_ema = _cpu_load.value();
        }

        // open the next duty-cycle window, idle total from scratch
        _last_release_us = now_us;
        _slept_us = 0;
    }

    void LoopScheduler::_record_tps(uint64_t now_ms) {
#ifdef TARGET_SIMULATOR
        // TPS is the one metric expressed per *real* second: under
        // BOTANIK_TIME_SCALE the loop runs faster, so ticks/real-second shows
        // whether the host sustains the sped-up rate. Hardware has no dilation,
        // so its uptime is already real.
        now_ms = HAL::Clock::real_uptime_ms();
#endif
        // achieved tick rate: ticks since the window anchor over the elapsed
        // window, normalized to per-second and published once the window
        // reaches a second.
        if (!_tps_open) {                     // first beat: open the window
            _tps_open         = true;
            _tps_anchor_ms    = now_ms;
            _tps_anchor_count = _context.tick_count;
            return;
        }
        uint64_t elapsed = now_ms - _tps_anchor_ms;
        if (elapsed >= 1000) {
            uint32_t ticks = _context.tick_count - _tps_anchor_count;
            _context.ticks_per_second = (uint32_t)((uint64_t)ticks * 1000 / elapsed);
            _tps_anchor_ms    = now_ms;
            _tps_anchor_count = _context.tick_count;
        }
    }

    void LoopScheduler::_sample_work(uint64_t now_us) {
        uint32_t busy = (now_us >= _work_start_us)
                      ? static_cast<uint32_t>(now_us - _work_start_us) : 0;
        if (busy > _cpu_loop_us_max) _cpu_loop_us_max = busy;
        // age by elapsed beats
        _cpu_loop_us_window.push(busy, 1 + _missed_this_beat);

        // Scan the stack high-water. The sim repaints the free stack each slice
        // and scans the sentinel (STACK_SLICE_TICKS throttles the repaint); OPTA
        // reads the monotonic RTOS watermark, so it needs neither the paint nor
        // the throttle - just sample it each beat.
#ifdef TARGET_SIMULATOR
        if (++_stack_slice_tick >= Config::STACK_SLICE_TICKS) {
            _stack_slice_tick = 0;
            _measure_stack_usage();
            _repaint_pending = true;
        }
#else
        _measure_stack_usage();
#endif

        _context.cpu_loop_us_max    = _cpu_loop_us_max;
        _context.cpu_loop_us_recent = _cpu_loop_us_window.value();
    }

    void LoopScheduler::_measure_stack_usage() {
        // High-water used bytes. On hardware the RTOS already watermarks each
        // thread's stack (painted once at thread creation), so we just read
        // it - no manual paint of the wrong (MSP) stack. NOTE: that watermark
        // is ALL-TIME (monotonic, can't reset), so on OPTA the all-time peak
        // (stack_bytes_max) is exact, but the per-slice "recent" window/EMA
        // degenerate to that same all-time figure. The sim repaints each slice,
        // so there it's a true recent high-water that can fall.
        uint32_t low = HAL::Stack::high();
#ifdef TARGET_SIMULATOR
        // The host has no RTOS watermark: read back the sentinel painted below
        // the loop frame. Scan the painted region top-down for the first
        // RUN-long run of intact sentinel words - the start of untouched stack.
        // A multi-word run makes a coincidental match in used data (~1/2^32 per
        // word) impossible, so this is the true high-water without the old
        // (non-monotone, unsound) bisection.
        if (_paint_top != nullptr) {
            const uint32_t PAT = (uint32_t)0x01010101u * Config::STACK_PAINT_BYTE;
            const int RUN = 4;   // 16 bytes of intact pattern == untouched
            // word-align down
            const uint32_t* top = reinterpret_cast<const uint32_t*>(
                reinterpret_cast<uintptr_t>(_paint_top) & ~(uintptr_t)3u);
            const uint32_t* bot = reinterpret_cast<const uint32_t*>(
                (reinterpret_cast<uintptr_t>(_paint_bot) + 3u) & ~(uintptr_t)3u);
            // default: the whole painted region was used
            const uint32_t* hw = bot;
            int run = 0;
            for (const uint32_t* p = top; p > bot; ) {
                --p;
                // hw = p + RUN is the first used word above the run
                if (*p == PAT) { if (++run >= RUN) { hw = p + RUN; break; } }
                else           { run = 0; }
            }
            low = (uint32_t)(_stack_base - reinterpret_cast<uintptr_t>(hw));
        }
#endif
        if (low == 0 || _stack_depth == 0) return;   // nothing measurable yet

        // The high-water is already a max; just track the all-time peak - no
        // EMA / rolling window. Publish the usage fraction and the absolute
        // worst-case headroom (least free stack ever = depth - peak used), the
        // safety-relevant value.
        if (low > _stack_bytes_max) _stack_bytes_max = low;
        _context.stack_usage      = static_cast<float>(_stack_bytes_max)
                                  / static_cast<float>(_stack_depth);
        _context.stack_bytes_free = _stack_depth - _stack_bytes_max;
    }

#ifdef TARGET_SIMULATOR

    // Simulator only: hardware uses the RTOS stack watermark instead (see
    // _measure_stack_usage), and manually repainting an RTOS thread's stack
    // would clobber the RTOS's own watermark fill. noinline is load-bearing: it
    // paints below its own &marker, so if it folded into the caller the paint
    // would clobber the caller's live locals.
    __attribute__((noinline)) void LoopScheduler::_paint_below() {
        if (_stack_base == 0) return;
        // Ceiling = our own frame (a local), so the region is below the CALLER
        // regardless of how inlining grows its frame - no dependency on the
        // caller's size. Back off a margin that clears this function's own
        // locals + memset's frame (the only thing below us); painting below
        // &marker without it would clobber our spilled locals. The margin need
        // only cover memset (a known leaf), not an unknown caller frame. memset
        // (bounds captured once) is correct even at -O0, unlike an inline loop
        // that re-reads now-clobbered local bounds each iteration.
        volatile uint8_t marker = 0;
        const ptrdiff_t MARGIN = 512;
        uint8_t* paint_top  = const_cast<uint8_t*>(&marker) - MARGIN;
        uint8_t* abs_bottom =
            reinterpret_cast<uint8_t*>(_stack_base - _stack_depth);
        if (paint_top <= abs_bottom) return;
        uint8_t* paint_bot = paint_top - Config::STACK_SCAN_LIMIT;
        if (paint_bot < abs_bottom) paint_bot = abs_bottom;
        memset(paint_bot, Config::STACK_PAINT_BYTE,
               (size_t)(paint_top - paint_bot));
        _paint_top = paint_top;
        _paint_bot = paint_bot;
    }

    // Simulator utilities (memlock, CPU-pinning and SCHED_FIFO)
    // ------------------------------------------------------------------------

    // BOTANIK_NO_RT_WARNING gates the best-effort failure warnings: a
    // constrained host (CI/sandbox) denies these privileges, and which it
    // denies varies, so the warnings are host-dependent noise the screen
    // goldens silence. The call itself still runs either way.
    static bool _rt_warn(Logs::Logger* logger) {
        return logger != nullptr && HAL::Env::u8("BOTANIK_NO_RT_WARNING", 0) == 0;
    }

    void lock_process_memory(Logs::Logger* logger) {
        // Lock pages into RAM (avoid page-fault latency). Process-wide incl.
        // future mappings, so it's a one-time call.
        if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1 && _rt_warn(logger))
            logger->warning(Logs::BOOT, "mlockall failed: \n%s", strerror(errno));
    }

    void make_thread_realtime(int cpu_core_id, Logs::Logger* logger) {
        const bool warn = _rt_warn(logger);
        // SCHED_FIFO + priority; pid 0 = the calling thread.
        struct sched_param param;
        param.sched_priority = 95; // 1 (low) .. 99 (high)
        if (sched_setscheduler(0, SCHED_FIFO, &param) == -1 && warn)
            logger->warning(Logs::BOOT, "sched_setscheduler failed: \n%s",
                            strerror(errno));

        // pin to a core to prevent migration
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(cpu_core_id, &mask);
        if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) == -1 && warn)
            logger->warning(Logs::BOOT, "sched_setaffinity failed: \n%s",
                            strerror(errno));
    }

#endif

} }
