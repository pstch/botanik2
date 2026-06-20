// TODO: AnsiTerminal still reads the concrete SchedulerContext/PlcSyncContext;
// a future unified Context wrapper could decouple it from both. See TODO.md.
/**
 * @file scheduler.cpp
 * @brief LoopScheduler implementation - tick timing, overrun reporting, and the
 *        out-of-line performance instrumentation (work-duration sampling, CPU
 *        duty cycle, jitter filtering, stack painting + high-water scan). The hot
 *        tick() path and the stack paint live inline in the header.
 */
#include <cstddef>
#include <cstring>
#ifdef TARGET_SIMULATOR
#include <cstdlib>   // getenv - BOTANIK_NO_XRUN_WARNING
#include <cerrno>
#include <sched.h>
#include <sys/mman.h>
#endif

#include "libbotanik_config.h"

#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/plcio/vars.h"

namespace Botanik { namespace Scheduler {

    // Simulator-only test hook: BOTANIK_NO_XRUN_WARNING silences the overrun
    // warning, whose count/placement are load-dependent (dilated time provokes
    // them) and would otherwise pollute screen snapshots. Read once.
    static bool _xrun_warning_off() {
#ifdef TARGET_SIMULATOR
        static const bool off = getenv("BOTANIK_NO_XRUN_WARNING") != nullptr;
        return off;
#else
        return false;
#endif
    }

    // Lifecycle
    // ------------------------------------------------------------------------

    LoopScheduler::LoopScheduler(uint32_t period_ms, Logs::Logger* logger)
        : _period_ms(period_ms), _logger(logger) {
        // measure stack base and depth
        _stack_base  = HAL::get_stack_base();
        _stack_depth = HAL::get_stack_depth();

        // configure EMA filters
        _cpu_load.configure(Utils::ema_alpha(Config::CPU_LOAD_EMA_ATTACK_MS),
                            Utils::ema_alpha(Config::CPU_LOAD_EMA_DECAY_MS));
        _jitter.configure(Utils::ema_alpha(Config::CPU_JITTER_EMA_ATTACK_MS),
                          Utils::ema_alpha(Config::CPU_JITTER_EMA_DECAY_MS));
        _stack_usage.configure(Utils::ema_alpha(Config::STACK_USAGE_EMA_ATTACK_MS, Config::STACK_SLICE_MS),
                               Utils::ema_alpha(Config::STACK_USAGE_EMA_DECAY_MS,  Config::STACK_SLICE_MS));

        // configure rolling-max windows
        _cpu_loop_us_window.configure(Config::LOAD_METRIC_WINDOW);
        _jitter_window.configure(Config::LOAD_METRIC_WINDOW);

        reset();
    }

    void LoopScheduler::reset() {
        _next_tick_ms = HAL::uptime_ms() + _period_ms;

        // reset CPU duty-cycle state
        _cpu_load.reset();
        _slept_us = 0;
        _last_release_us = 0;

        // reset work-duration state
        _measuring       = false;
        _cpu_loop_us_max = 0;
        _cpu_loop_us_window.reset();

        // reset jitter state
        _jitter.reset();
        _jitter_window.reset();
        _jitter_us_max = 0;

        // reset stack state
        _stack_bytes_max = 0;
        _stack_usage.reset();
        for (uint32_t i = 0; i < Config::LOAD_METRIC_BUCKETS; ++i) _stack_slices[i] = 0;
        _stack_head       = 0;
        _stack_slice_tick = 0;
        _repaint_pending  = true;

        // reset tick-rate state
        _tps_anchor_ms    = 0;
        _tps_anchor_count = 0;

        // clear the published metric fields (leave tick_count/overruns/timestamp)
        _context.ticks_per_second     = 0;
        _context.cpu_load_ema         = 0.0f;
        _context.cpu_loop_us_recent   = 0;
        _context.cpu_loop_us_max      = 0;
        _context.cpu_jitter_ema       = 0.0f;
        _context.cpu_jitter_us_recent = 0;
        _context.cpu_jitter_us_max    = 0;
        _context.stack_usage_ema      = 0.0f;
        _context.stack_bytes_recent   = 0;
        _context.stack_bytes_max      = 0;
        _context.stack_depth          = _stack_depth;

        // log the first overrun after a reset immediately
        _overrun_log_last_ms = 0;
        _overrun_pending = 0;
    }

    // Tick
    // ------------------------------------------------------------------------
    // tick() is defined inline in the header (always_inline + BOTANIK_NO_ASAN) so its
    // trailing stack paint folds into the caller's frame and captures the loop body's
    // SP. The cold helpers below stay out of line.

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
            // now / _overrun_log_last_ms are 64-bit uptime (not the 32-bit timestamp_ms),
            // so this subtraction never wraps - it is not the wrap-safe-32 idiom.
            if (_logger != nullptr && !_xrun_warning_off() &&
                now - _overrun_log_last_ms >= Config::SCHEDULER_XRUN_LOG_MS) {
                _logger->warning(PlcIO::STATUS, "scheduler overrun: skipped %lu beat(s)",
                    (unsigned long)_overrun_pending);
                _overrun_log_last_ms = now;
                _overrun_pending = 0;
            }
        }
    }

    void LoopScheduler::_record_jitter(uint32_t jitter_us) {
        // measure instantaneous jitter as a fraction of the tick interval
        float instant = (_period_ms > 0) ? static_cast<float>(jitter_us)
                        / static_cast<float>(_period_ms * 1000u) : 0.0f;
        // update peak jitter time value
        if (jitter_us > _jitter_us_max) _jitter_us_max = jitter_us;
        // push jitter time into the rolling-max window
        _jitter_window.push(jitter_us);
        // push jitter fraction into the EMA
        _jitter.push(instant);

        // update published context values
        _context.cpu_jitter_ema       = _jitter.value();
        _context.cpu_jitter_us_recent = _jitter_window.value();
        _context.cpu_jitter_us_max    = _jitter_us_max;
    }

    void LoopScheduler::_record_load(uint64_t now_us) {
        // duty cycle: active (non-slept) time over the nominal period. Normalizing by
        // the period (not elapsed) lets an overrunning beat read past 100%.
        if (_last_release_us != 0) {
            uint64_t elapsed = now_us - _last_release_us;
            uint64_t active  = (elapsed > _slept_us) ? (elapsed - _slept_us) : 0;
            float instant    = (_period_ms > 0) ? static_cast<float>(active)
                             / static_cast<float>(_period_ms * 1000u) : 0.0f;
            _cpu_load.push(instant);
            _context.cpu_load_ema = _cpu_load.value();
        }

        // open the next duty-cycle window and start its idle total from scratch
        _last_release_us = now_us;
        _slept_us = 0;
    }

    void LoopScheduler::_record_tps(uint64_t now_ms) {
#ifdef TARGET_SIMULATOR
        // TPS is the one metric expressed per *real* second: under BOTANIK_TIME_SCALE
        // the loop runs faster, so ticks/real-second shows whether the host sustains
        // the sped-up rate. Hardware has no dilation, so its uptime is already real.
        now_ms = HAL::real_uptime_ms();
#endif
        // achieved tick rate: ticks since the window anchor over the elapsed window,
        // normalized to per-second and published once the window reaches a second.
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
        // measure the work-window duration (peak single-beat work time)
        uint32_t busy = (now_us >= _work_start_us)
                      ? static_cast<uint32_t>(now_us - _work_start_us) : 0;
        if (busy > _cpu_loop_us_max) _cpu_loop_us_max = busy;
        _cpu_loop_us_window.push(busy);

        // scan stack usage if we are at a stack slice boundary
        if (++_stack_slice_tick >= Config::STACK_SLICE_TICKS) {
            _stack_slice_tick = 0;
            _measure_stack_usage();
            _repaint_pending = true;
        }

        // update published work-duration values
        _context.cpu_loop_us_max    = _cpu_loop_us_max;
        _context.cpu_loop_us_recent = _cpu_loop_us_window.value();
    }

    void LoopScheduler::_log_startup() {
        if (_logger != nullptr)
            _logger->info(PlcIO::BOOT, "scheduler started, period=%ums", (unsigned)_period_ms);
    }

    void LoopScheduler::_measure_stack_usage() {
        // High-water used bytes. On hardware the RTOS already watermarks each thread's
        // stack (painted once at thread creation), so we just read it - no manual paint
        // of the wrong (MSP) stack. NOTE: that watermark is ALL-TIME (monotonic, can't
        // reset), so on OPTA the all-time peak (stack_bytes_max) is exact, but the
        // per-slice "recent" window/EMA degenerate to that same all-time figure. The
        // sim repaints each slice, so there it's a true recent high-water that can fall.
        uint32_t low = HAL::stack_high_water_bytes();
#ifdef TARGET_SIMULATOR
        // The host has no RTOS watermark: read back the sentinel painted below the loop
        // frame. Scan the painted region top-down for the first RUN-long run of intact
        // sentinel words - the start of untouched stack. A multi-word run makes a
        // coincidental match in used data (~1/2^32 per word) impossible, so this is the
        // true high-water without the old (non-monotone, unsound) bisection.
        if (_paint_top != nullptr) {
            const uint32_t PAT = (uint32_t)0x01010101u * Config::STACK_PAINT_BYTE;
            const int RUN = 4;   // 16 bytes of intact pattern == untouched
            const uint32_t* top = reinterpret_cast<const uint32_t*>(
                reinterpret_cast<uintptr_t>(_paint_top) & ~(uintptr_t)3u);   // word-align down
            const uint32_t* bot = reinterpret_cast<const uint32_t*>(
                (reinterpret_cast<uintptr_t>(_paint_bot) + 3u) & ~(uintptr_t)3u);
            const uint32_t* hw = bot;   // default: the whole painted region was used
            int run = 0;
            for (const uint32_t* p = top; p > bot; ) {
                --p;
                if (*p == PAT) { if (++run >= RUN) { hw = p + RUN; break; } }  // first used word above the run
                else           { run = 0; }
            }
            low = (uint32_t)(_stack_base - reinterpret_cast<uintptr_t>(hw));
        }
#endif
        if (low == 0 || _stack_depth == 0) return;   // nothing measurable yet

        // measure stack usage as a fraction of the stack depth
        float ratio = static_cast<float>(low) / static_cast<float>(_stack_depth);
        // push stack usage fraction into the EMA
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

        // update published context values
        _context.stack_usage_ema    = _stack_usage.value();
        _context.stack_bytes_max    = _stack_bytes_max;
        _context.stack_bytes_recent = recent;
    }

    // Simulator only: hardware uses the RTOS stack watermark instead (see
    // _measure_stack_usage), and manually repainting an RTOS thread's stack would
    // clobber the RTOS's own watermark fill.
    void LoopScheduler::_paint_below() {
        if (_stack_base == 0) return;
        // Ceiling = our own frame (a local), so the region is below the CALLER regardless
        // of how inlining grows its frame - no dependency on the caller's size. Back off a
        // margin that clears this function's own locals + memset's frame (the only thing
        // below us); painting below &marker without it would clobber our spilled locals.
        // The margin need only cover memset (a known leaf), not an unknown caller frame.
        // memset (bounds captured once) is correct even at -O0, unlike an inline loop that
        // re-reads now-clobbered local bounds each iteration.
        volatile uint8_t marker = 0;
        const ptrdiff_t MARGIN = 512;
        uint8_t* paint_top  = const_cast<uint8_t*>(&marker) - MARGIN;
        uint8_t* abs_bottom = reinterpret_cast<uint8_t*>(_stack_base - _stack_depth);
        if (paint_top <= abs_bottom) return;
        uint8_t* paint_bot = paint_top - Config::STACK_SCAN_LIMIT;
        if (paint_bot < abs_bottom) paint_bot = abs_bottom;
        memset(paint_bot, Config::STACK_PAINT_BYTE, (size_t)(paint_top - paint_bot));
        _paint_top = paint_top;
        _paint_bot = paint_bot;
    }

#ifdef TARGET_SIMULATOR

    // Simulator utilities (memlock, CPU-pinning and SCHED_FIFO)
    // ------------------------------------------------------------------------

    void lock_process_memory(Logs::Logger* logger) {
        // lock memory pages into RAM to avoid page fault latency. Process-wide
        // and covers future mappings too, so this is a one-time call.
        if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1 && logger != nullptr)
            logger->warning(PlcIO::BOOT, "mlockall failed: \n%s", strerror(errno));
    }

    void make_thread_realtime(int cpu_core_id, Logs::Logger* logger) {
        // set the real-time scheduling policy (SCHED_FIFO) and priority. pid 0
        // targets the calling thread, so this only affects this thread.
        struct sched_param param;
        param.sched_priority = 95; // Priorities range from 1 (low) to 99 (high)
        if (sched_setscheduler(0, SCHED_FIFO, &param) == -1 && logger != nullptr)
            logger->warning(PlcIO::BOOT, "sched_setscheduler failed: \n%s", strerror(errno));

        // pin the thread to a dedicated CPU core to prevent task migration
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(cpu_core_id, &mask);
        if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) == -1 && logger != nullptr)
            logger->warning(PlcIO::BOOT, "sched_setaffinity failed: \n%s", strerror(errno));
    }

#endif

} }
