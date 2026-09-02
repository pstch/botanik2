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

#include <cstdio>

#include <cstddef>
#include <cstring>

#include "libbotanik/config/scheduler.h"

#include "libbotanik/hal/platform.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/logs/topics.h"

#ifdef TARGET_SIMULATOR
// Host-only: pin the sim to a real-time policy so its beat is not at the mercy
// of the desktop scheduler. No equivalent (or need) on the OPTA's RTOS.
#include <cerrno>
#include <sched.h>
#include <sys/mman.h>
#endif


namespace Botanik { namespace Scheduler {

    namespace {
        /** Metric window expressed in per-tick samples, which is what the
         *  rolling-max filters are configured with. */
        constexpr uint32_t METRIC_WINDOW_SAMPLES =
            Config::LOAD_METRIC_WINDOW_MS / Config::TASK_PERIOD_MS;

        /** Ticks between stack measurements. Not every beat: the host's is a
         *  scan and a repaint of up to 64 KB, and the OPTA's is an all-time
         *  watermark that loses nothing by being read less often. */
        constexpr uint32_t STACK_SLICE_TICKS =
            METRIC_WINDOW_SAMPLES >= Config::LOAD_METRIC_BUCKETS
                ? METRIC_WINDOW_SAMPLES / Config::LOAD_METRIC_BUCKETS : 1u;
    }

    // Lifecycle

    LoopScheduler::LoopScheduler(Tasks::Registry& tasks, uint32_t period_ms,
                                 Logs::Logger* logger)
        : _tasks(tasks), _period_ms(period_ms), _logger(logger) {
        _stack_depth = HAL::Stack::high_water().depth;

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
    // tick() is inline in the header (always_inline, see its @warning); the
    // cold helpers below stay out of line.

    // Internals

    void LoopScheduler::_skip_beats(uint64_t now) {
        _missed_this_beat = 0;
        if (_period_ms == 0) return;   // divides below; the ctor's contract is >= 2
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

    void LoopScheduler::_sample_work() {
        // Unsigned all the way, so the HRT's wrap needs no special case.
        const uint32_t busy =
            HAL::Clock::hrt_us(HAL::Clock::hrt_ticks() - _work_start_tk);
        if (busy > _cpu_loop_us_max) _cpu_loop_us_max = busy;
        // age by elapsed beats
        _cpu_loop_us_window.push(busy, 1 + _missed_this_beat);

        if (++_stack_slice_tick >= STACK_SLICE_TICKS) {
            _stack_slice_tick = 0;
            _measure_stack_usage();
        }

        _context.cpu_loop_us_max    = _cpu_loop_us_max;
        _context.cpu_loop_us_recent = _cpu_loop_us_window.value();
    }

    void LoopScheduler::_measure_stack_usage() {
        // One question, one answer: the HAL owns how a stack is measured on
        // this platform. The reading is already a max (an all-time RTOS
        // watermark on OPTA, the last slice's high-water on the host), so the
        // only filter is the all-time peak below.
        const HAL::Stack::StackUsage u = HAL::Stack::high_water();
        if (u.depth != 0) _stack_depth = u.depth;
        if (u.used == 0 || _stack_depth == 0) return;   // nothing measurable yet

        // The usage fraction, and the worst-case headroom (least free stack
        // ever = depth - peak used), which is the safety-relevant number.
        if (u.used > _stack_bytes_max) _stack_bytes_max = u.used;
        _context.stack_depth      = _stack_depth;
        _context.stack_usage      = static_cast<float>(_stack_bytes_max)
                                  / static_cast<float>(_stack_depth);
        _context.stack_bytes_free = _stack_depth - _stack_bytes_max;
    }

#ifdef TARGET_SIMULATOR

    // Simulator utilities (memlock, CPU-pinning and SCHED_FIFO)

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
