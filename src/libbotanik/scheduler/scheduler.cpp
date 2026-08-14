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
#include "libbotanik/scheduler/scheduler.h"

#include <cstdio>   // fprintf - host-side notes go to stderr, not the log

#include <cstddef>                          // size_t / ptrdiff_t
#include <cstring>                          // memset

#include "libbotanik/config/scheduler.h"  // Config::LOOP_* / TASK_* / GROUP_*

#include "libbotanik/hal/platform.h"        // HAL::Clock / Stack
#include "libbotanik/logs/logger.h"         // Logs::Logger
#include "libbotanik/logs/topics.h"         // Logs::STATUS

#ifdef TARGET_SIMULATOR
// Host-only: pin the sim to a real-time policy so its beat is not at the mercy
// of the desktop scheduler. No equivalent (or need) on the OPTA's RTOS.
#include <cerrno>                           // errno
#include <sched.h>                          // sched_setscheduler / SCHED_FIFO
#include <sys/mman.h>                       // mlockall / MCL_CURRENT
#endif


namespace Botanik { namespace Scheduler {

    namespace {
        /** Metric window expressed in per-tick samples, which is what the
         *  rolling-max filters are configured with. */
        constexpr uint32_t METRIC_WINDOW_SAMPLES =
            Config::LOAD_METRIC_WINDOW_MS / Config::TASK_PERIOD_MS;

#ifdef TARGET_SIMULATOR
        /** Byte pattern for stack painting. Arbitrary by construction: the scan
         *  broadcasts it to a word and hunts a run of them, so any non-degenerate
         *  value works. */
        constexpr uint8_t  STACK_PAINT_BYTE  = 0xCD;

        /** Bytes painted/scanned below the loop frame, on top of the clamp
         *  against the real stack bottom. A host-stack property, not a site's. */
        constexpr uint32_t STACK_SCAN_LIMIT  = 65536;

        /** High-water is measured (and the free stack re-painted) once per slice
         *  rather than every tick, to amortise the paint/scan cost. */
        constexpr uint32_t STACK_SLICE_TICKS =
            METRIC_WINDOW_SAMPLES >= Config::LOAD_METRIC_BUCKETS
                ? METRIC_WINDOW_SAMPLES / Config::LOAD_METRIC_BUCKETS : 1u;
#endif
    }

    // Lifecycle
    // ------------------------------------------------------------------------

    LoopScheduler::LoopScheduler(Tasks::Registry& tasks, uint32_t period_ms,
                                 Logs::Logger* logger)
        : _tasks(tasks), _period_ms(period_ms), _logger(logger) {
        _stack_base  = HAL::Stack::base();
        _stack_depth = HAL::Stack::depth();

        _cpu_load.configure(Utils::ema_alpha(Config::CPU_LOAD_EMA_ATTACK_MS),
                            Utils::ema_alpha(Config::CPU_LOAD_EMA_DECAY_MS));
        _jitter.configure(Utils::ema_alpha(Config::CPU_JITTER_EMA_ATTACK_MS),
                          Utils::ema_alpha(Config::CPU_JITTER_EMA_DECAY_MS));
        _cpu_loop_us_window.configure(METRIC_WINDOW_SAMPLES);
        _jitter_window.configure(METRIC_WINDOW_SAMPLES);

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
    // tick() is inline in the header (always_inline + BOTANIK_NO_ASAN, see its
    // @warning); the cold helpers below stay out of line.

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
            // No log line: it would flood.
            // Surfaced by the CPU_XRUN alarm and the `stats` overrun count.
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
        // Active (non-slept) time over the NOMINAL period, not the elapsed one,
        // so an overrunning beat reads past 100%.
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
        // the manual clock the loop runs on demand, so ticks/real-second shows
        // whether the host sustains the beat rate. Hardware runs free,
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

        // Stack high-water: the sim repaints and scans the sentinel once per
        // slice (STACK_SLICE_TICKS), OPTA samples the monotonic RTOS watermark
        // each beat and needs neither paint nor throttle.
#ifdef TARGET_SIMULATOR
        if (++_stack_slice_tick >= STACK_SLICE_TICKS) {
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
        // High-water used bytes. On hardware the RTOS watermarks each thread's
        // stack, so just read it (a manual paint would hit the wrong, MSP,
        // stack). That watermark is ALL-TIME and cannot reset, so on OPTA the
        // all-time peak is exact while any "recent" figure degenerates to it;
        // the sim repaints each slice and its high-water can fall.
        uint32_t low = HAL::Stack::high();
#ifdef TARGET_SIMULATOR
        // The host has no RTOS watermark: read back the sentinel painted below
        // the loop frame. Scan the painted region top-down for the first
        // RUN-long run of intact sentinel words, the start of untouched stack.
        // A multi-word run makes a coincidental match in used data (~1/2^32 per
        // word) impossible.
        if (_paint_top != nullptr) {
            const uint32_t PAT = (uint32_t)0x01010101u * STACK_PAINT_BYTE;
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

        // The high-water is already a max, so only the all-time peak is kept.
        // Publish the usage fraction and the worst-case headroom (least free
        // stack ever = depth - peak used), which is the safety-relevant value.
        if (low > _stack_bytes_max) _stack_bytes_max = low;
        _context.stack_usage      = static_cast<float>(_stack_bytes_max)
                                  / static_cast<float>(_stack_depth);
        _context.stack_bytes_free = _stack_depth - _stack_bytes_max;
    }

#ifdef TARGET_SIMULATOR

    // Simulator only: repainting an RTOS thread's stack would clobber the
    // watermark hardware reads instead (see _measure_stack_usage). noinline is
    // load-bearing: it paints below its own &marker, so folded into the caller
    // the paint would clobber the caller's live locals.
    __attribute__((noinline)) void LoopScheduler::_paint_below() {
        if (_stack_base == 0) return;
        // Ceiling = our own frame (a local), so the region is below the CALLER
        // however inlining grows its frame. The margin must clear this
        // function's own spilled locals plus memset's frame (a known leaf, the
        // only thing below us). memset captures its bounds once, so it is
        // correct even at -O0, where an inline loop would re-read bounds it has
        // just clobbered.
        volatile uint8_t marker = 0;
        const ptrdiff_t MARGIN = 512;
        // Through uintptr_t deliberately: this walks the stack, it does not
        // index `marker`, and pointer arithmetic off a one-byte object is
        // exactly what -Warray-bounds is there to catch everywhere else.
        uint8_t* paint_top = reinterpret_cast<uint8_t*>(
            reinterpret_cast<uintptr_t>(const_cast<uint8_t*>(&marker))
                - (uintptr_t)MARGIN);
        uint8_t* abs_bottom =
            reinterpret_cast<uint8_t*>(_stack_base - _stack_depth);
        if (paint_top <= abs_bottom) return;
        uint8_t* paint_bot = paint_top - STACK_SCAN_LIMIT;
        if (paint_bot < abs_bottom) paint_bot = abs_bottom;
        memset(paint_bot, STACK_PAINT_BYTE,
               (size_t)(paint_top - paint_bot));
        _paint_top = paint_top;
        _paint_bot = paint_bot;
    }

    // Simulator utilities (memlock, CPU-pinning and SCHED_FIFO)
    // ------------------------------------------------------------------------

    // BOTANIK_NO_RT_WARNING gates the best-effort failure warnings: which
    // privileges a constrained host (CI/sandbox) denies varies, so the warnings
    // are host-dependent noise the screen goldens silence. The call itself runs
    // either way.
    static bool _rt_warn() {
        return HAL::Env::u8("BOTANIK_NO_RT_WARNING", 0) == 0;
    }

    /** @brief Say so on stderr with an area tag, NOT through the logger: these
     *  are facts about the development box, and the log is the firmware's
     *  account of the plant (and reaches the LOGS page and the MQTT log
     *  topic). */
    static void _rt_say(const char* what, int err) {
        if (!_rt_warn()) return;
        fprintf(stderr, "[sim-rt] %s: %s\n", what, strerror(err));
        fflush(stderr);
    }

    void lock_process_memory() {
        // Lock pages into RAM (avoid page-fault latency). Process-wide incl.
        // future mappings, so it's a one-time call.
        if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) _rt_say("mlockall", errno);
    }

    void make_thread_realtime(int cpu_core_id) {
        // SCHED_FIFO + priority; pid 0 = the calling thread.
        struct sched_param param;
        param.sched_priority = 95; // 1 (low) .. 99 (high)
        if (sched_setscheduler(0, SCHED_FIFO, &param) == -1)
            _rt_say("sched_setscheduler", errno);

        // pin to a core to prevent migration
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(cpu_core_id, &mask);
        if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) == -1)
            _rt_say("sched_setaffinity", errno);
    }

#endif

} }

namespace Botanik { namespace Scheduler {

    void LoopScheduler::setup(uint64_t now_ms, uint32_t watchdog_ms) {
        _tasks.setup(now_ms);
        HAL::Watchdog::start(watchdog_ms);
        _context.watchdog_ms = watchdog_ms;
    }

    void LoopScheduler::run_beat(uint64_t now_ms) {
        // This order IS the data flow; it must not become configurable.
        _tasks.run_phase(Tasks::PHASE_INPUT,   now_ms);
        _tasks.run_phase(Tasks::PHASE_CONFIG,  now_ms);
        _tasks.run_phase(Tasks::PHASE_SERVICE, now_ms);
        _tasks.run_phase(Tasks::PHASE_CONTROL, now_ms);
        _tasks.run_phase(Tasks::PHASE_OUTPUT,  now_ms);
        _tasks.run_phase(Tasks::PHASE_PRESENT, now_ms);
    }

    bool LoopScheduler::kick(bool healthy) {
        _context.watchdog_kicking = healthy;
        if (!healthy) {
            if (_context.watchdog_withheld != 0xFFFFFFFFu) _context.watchdog_withheld++;
            return false;
        }
        HAL::Watchdog::kick();
        _context.watchdog_kick_ms = HAL::Clock::uptime_ms();
        return true;
    }

}}  // namespace Botanik::Scheduler
