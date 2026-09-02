// REVIEW_35: WIP
#ifndef SCHEDULER_SCHEDULER_H
#define SCHEDULER_SCHEDULER_H

/**
 * @file scheduler.h
 * @brief Non-blocking fixed-period loop driver with perf monitoring. */

#include <cstdint>

#include "libbotanik/config/scheduler.h"

#include "libbotanik/hal/platform.h"
#include "libbotanik/tasks/tasks.h"
#include "libbotanik/utils/filters.h"

// forward declare the Logger pointer type
namespace Botanik { namespace Logs { class Logger; } }

namespace Botanik { namespace Scheduler {

    /** @brief The rolling-max window the per-beat metrics use. Bound here, not
     *  in utils/filters.h: the bucket count is a scheduler tuning knob. */
    typedef Utils::WindowedMax<Config::LOAD_METRIC_BUCKETS> MetricWindow;

    /** @brief Where the unit is in its life, from power-on to its ending.
     *
     *  ONE state, so the two ends of the run are the same mechanism rather
     *  than a startup timer in one place and a shutdown timer in another.
     *  What reads it gates on RUN and needs to know nothing else.
     *
     *  The firmware advances it; the scheduler only holds it, because the
     *  transitions depend on things the loop driver has no business knowing -
     *  when setup() returned, two operator parameters, and whether the health
     *  monitor has been asked to stop. */
    enum class RunState : uint8_t {
        SETUP,   ///< inside setup(): components exist, the beat has not begun
        START,   ///< the beat runs, the laws do not - waiting out sys_startup_sec
        RUN,     ///< in service
        STOP,    ///< the laws have stopped - waiting out sys_shutdown_sec
        HALT     ///< done: waiting for the ending HealthMonitor::Stop names
    };

    /** @brief Stable metrics snapshot consumed by AnsiTerminal.
     *  @note Written by LoopScheduler, read by other components. */
    struct SchedulerContext {
        /** @brief Where the unit is in its life. @see RunState */
        RunState run_state = RunState::SETUP;

        float    cpu_load_ema       = 0.0f;     ///< EMA of CPU load (duty cycle, fraction of period)
        uint32_t cpu_loop_us_recent = 0;        ///< Windowed peak work duration, us
        uint32_t cpu_loop_us_max    = 0;        ///< All-time peak work duration, us

        float    stack_usage        = 0.0f;     ///< Peak stack usage, fraction
        uint32_t stack_bytes_free   = 0;        ///< Minimum free stack ever, bytes
        uint32_t stack_depth        = 0;        ///< Total thread stack allocation, bytes

        uint32_t ticks_per_second   = 0;        ///< Measured actual loop rate (TPS)

        float    cpu_jitter_ema       = 0.0f;   ///< EMA of scheduling jitter (fraction of interval)
        uint32_t cpu_jitter_us_recent = 0;      ///< Windowed peak CPU jitter, us
        uint32_t cpu_jitter_us_max    = 0;      ///< All-time peak CPU jitter, us
        uint32_t tick_count      = 0;           ///< Tick count since init
        uint32_t overruns        = 0;           ///< Cumulative beats skipped to overruns

        /** @brief Loop "now" (uptime ms), republished by tick() when the work window opens. */
        uint64_t timestamp_ms    = 0;

        /** @name Watchdog
         *
         *  What the loop is doing about the reset it can choose to take. Here
         *  because the SCHEDULER owns the watchdog, and a withheld kick is
         *  otherwise invisible until the unit reboots.
         *  @{ */
        /** @brief Armed timeout in ms; 0 before setup() arms it. */
        uint32_t watchdog_ms      = 0;
        /** @brief Whether the last kick went out. False means the loop has
         *  decided to let the hardware reset the unit. */
        bool     watchdog_kicking = true;
        /** @brief Kicks withheld since boot. Non-zero after a recovery says the
         *  unit came within a timeout of resetting itself. */
        uint32_t watchdog_withheld = 0;
        /** @brief Uptime of the last kick that WENT OUT, in ms; 0 before the
         *  first. Read against the clock, so how close the unit is to its own
         *  reset is a subtraction rather than a guess. */
        uint64_t watchdog_kick_ms = 0;
        /** @} */
    };

    /** @class LoopScheduler
     *  @brief Non-blocking fixed-period loop driver: releases ticks at the
     *         configured period, absorbs overruns, and measures its own per-beat
     *         performance (jitter, CPU duty cycle, work duration, stack usage),
     *         publishing everything into the SchedulerContext.
     *  @note Not a Component: it is the loop *driver* (it produces the beat and
     *        the timestamp the Components are stepped with), so it keeps tick()
     *        and does its own one-shot boot announce. */
    class LoopScheduler {
    private:
        /** @name State
         *  @{ */

        // tick() sleeps until 1 ms before the grid point and busy-waits the
        // last ms, so a period under 2 ms would be all spin.
        static_assert(Config::TASK_PERIOD_MS >= 2, "TASK_PERIOD_MS must be >= 2 (sleep/spin split)");

        /** @brief The table this paces, BORROWED - owned by the composition
         *  root, because a Component is constructed with the registry it will
         *  register in, and this class is constructed with the Logger.
         *  @note This class decides WHEN the beat happens; the table decides
         *  WHAT runs in it (@ref dev_tasks). */
        Tasks::Registry&  _tasks;

        /** @brief Target loop period (ms). */
        uint32_t        _period_ms     = Config::TASK_PERIOD_MS;
        uint64_t        _next_tick_ms  = 0;   ///< uptime anchor of the next tick
        /** @brief One-shot startup message (may be null). */
        Logs::Logger*   _logger        = nullptr;
        bool            _announced      = false;   ///< first-tick boot announce latch
        /** @brief Beats skipped on THIS tick (0 normally); weights the metric
         *  windows so a catch-up ages them by wall-time, not one sample. */
        uint32_t        _missed_this_beat    = 0;
        SchedulerContext _context      = {};   ///< published metrics snapshot

        /** @} */

        /** @name Scheduling-jitter metrics
         *  @{ */

        Utils::ExpMovingAverage _jitter;          ///< EMA (fraction of tick interval)
        MetricWindow     _jitter_window;   ///< windowed peak, us
        uint32_t                _jitter_us_max = 0;   ///< all-time peak, us

        /** @} */

        /** @name CPU-load (duty cycle) metrics
         *  @brief Load is a beat's active (non-slept) time over the nominal
         *  period, so it counts work plus loop overhead plus the busy-wait, and
         *  an overrunning beat reads past 100%. Both terms come from actual
         *  uptime deltas, not the requested sleep.
         *  @{ */

        Utils::ExpMovingAverage _cpu_load;   ///< EMA of CPU duty cycle
        uint64_t                _slept_us = 0;   ///< time slept so far this beat, us
        /** @brief Previous release (duty-cycle window start). */
        uint64_t                _last_release_us = 0;

        /** @} */

        /** @name Work-duration metrics
         *  @{ */

        /** @brief True between a release and the next-tick sample. */
        bool              _measuring     = false;
        /** @brief HRT reading when the work window opened. Ticks, not us: the
         *  OPTA's uptime clock resolves ~30 us and a beat's work is measured in
         *  hundreds. @see HAL::Clock::hrt_ticks */
        uint32_t          _work_start_tk = 0;
        uint32_t          _cpu_loop_us_max = 0;      ///< all-time peak, us
        MetricWindow _cpu_loop_us_window;     ///< windowed peak, us

        /** @} */

        /** @name Stack-usage metrics
         *  @brief HAL::Stack::high_water() already answers with a max, so only
         *  the all-time peak is tracked here: no EMA or window on top of one.
         *  @{ */

        uint32_t          _stack_depth      = 0;   ///< total thread stack, bytes
        uint32_t          _stack_bytes_max  = 0;   ///< all-time peak used, bytes
        uint32_t          _stack_slice_tick = 0;   ///< ticks into the current slice

        /** @} */

        /** @name Tick-rate metric
         *  @{ */

        uint64_t          _tps_anchor_ms     = 0;   ///< one-second window anchor (ms)
        uint32_t          _tps_anchor_count  = 0;   ///< tick_count at the anchor
        /** @brief Window open? A flag, not an anchor==0 sentinel: the clock is
         *  legitimately 0 for the first ~1 ms. */
        bool              _tps_open          = false;

        /** @} */

    protected:

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Construct the loop scheduler.
         *  @param period_ms Target loop period (ms).
         *  @param logger    Optional logger for startup and periodic load
         *                   reports. */
        LoopScheduler(Tasks::Registry& tasks,
                      uint32_t period_ms = Config::TASK_PERIOD_MS,
                      Logs::Logger* logger = nullptr);

        /** @} */

        /** @name Tick
         *  @{ */

        /**
         * @brief Non-blocking check: is it time to run the beat? Each call closes
         *        the previous beat's work window and accumulates slept time; a
         *        release republishes "now", records jitter and duty cycle, and
         *        paints the free stack.
         * @warning always_inline: this runs on every pass of the main loop,
         *          the busy-wait included, so it folds into the caller and the
         *          cold helpers stay out of line.
         * @return true exactly ONCE per period when it is time to execute.
         */
        inline __attribute__((always_inline)) bool tick() {
            if (!_announced) { _announced = true; _announce(); }   // once, out of line
            // Derive ms from the same us reading so the jitter math can't
            // underflow against the grid (a separate uptime_ms() read could
            // straddle the ms boundary and land just under _next_tick_ms*1000,
            // wrapping the unsigned subtraction).
            uint64_t now_us = HAL::Clock::uptime_us();
            uint64_t now_ms = now_us / 1000;
            if (_measuring) { _measuring = false; _sample_work(); }
            int64_t remaining_ms = (int64_t)_next_tick_ms - (int64_t)now_ms;
            if (remaining_ms > 0) {
                // Sleep to the grid minus 1 ms (the last ~1 ms is busy-waited =
                // active CPU). Count the ACTUAL slept time, not the requested
                // one, or the duty cycle lies on overrun.
                if (remaining_ms > 1) {
                    uint64_t sleep_start_us = HAL::Clock::uptime_us();
                    HAL::Clock::sleep_ms((uint32_t)(remaining_ms - 1));
                    _slept_us += HAL::Clock::uptime_us() - sleep_start_us;
                }
                return false;   // busy-wait the last ms; defer work
            }
            // publish the loop "now" so consumers share one timestamp per tick
            _context.timestamp_ms = now_ms;
            _skip_beats(now_ms);   // no log; surfaced via CPU_XRUN + stats
            // Clamp grid-relative lateness at 0: _skip_beats keeps now >= grid
            // so it shouldn't underflow, but one wrapped sample would poison
            // the all-time max.
            uint64_t grid_us = _next_tick_ms * 1000;
            _record_jitter(now_us > grid_us ? (uint32_t)(now_us - grid_us) : 0);
            _record_load(now_us);
            _next_tick_ms += _period_ms;
            ++_context.tick_count;
            _record_tps(now_ms);
            // open the work window for the beat about to run
            _work_start_tk = HAL::Clock::hrt_ticks();
            _measuring     = true;
            return true;
        }

        /** @} */

        /** @name Metrics access
         *  @{ */

        /** @brief Stable metrics snapshot for external consumers (e.g. status
         *  bar). */
        SchedulerContext* context() { return &_context; }

        /** @} */

        /** @name The scan cycle
         *
         *  The scheduler paces the beat AND runs it: it owns the task table, the
         *  one-time init pass and the watchdog.
         *  @{ */

        /** @brief The task table every unit of work registers with. */
        Tasks::Registry&       tasks()       { return _tasks; }
        const Tasks::Registry& tasks() const { return _tasks; }

        /** @brief Run every registered one-time init, then arm the watchdog at
         *  @p watchdog_ms. Last thing before the loop starts.
         *
         *  @warning Call it once the log sinks exist: this is what runs each
         *  component's announce() and the HealthMonitor's POST. */
        void setup(uint64_t now_ms, uint32_t watchdog_ms);

        /** @brief Run one beat: every phase, in the order that IS the data flow.
         *  Not configurable, and not a list a caller assembles. */
        void run_beat(uint64_t now_ms);

        /** @brief Kick the watchdog while @p healthy; returns false when the
         *  kick was WITHHELD.
         *
         *  Withholding chooses to let the hardware reset us, so the caller drops
         *  the field to its safe state first: the reset gives no such guarantee,
         *  and the gap before the reboot is exactly when an actuator must not be
         *  left energised. The verdict stays the caller's - the scheduler owns
         *  the watchdog, not the judgement. */
        bool kick(bool healthy);

        /** @} */

    private:
        /** @name Internals
         *  @{ */

        /** @brief Out-of-line one-shot boot announce (keeps the full Logger out
         *  of this header and the hot tick() path). */
        void _announce();
        /** @brief Advance the next-tick anchor to skip missed beats when the
         *  loop falls behind. */
        void _skip_beats(uint64_t now);
        /** @brief @p us as a fraction of the nominal tick period (0 if the
         *  period is unset). Single-precision: the metric math must not promote
         *  (no double FPU on the M4). */
        float _fraction_of_period(uint64_t us) const {
            return (_period_ms > 0) ? (float)us / (float)(_period_ms * 1000u) : 0.0f;
        }
        /** @brief Filter a beat's scheduling jitter and publish it into the
         *  context. */
        void _record_jitter(uint32_t jitter_us);
        /** @brief Compute the beat's CPU duty cycle (active fraction of the
         *  elapsed beat) from the actual slept time and publish it. */
        void _record_load(uint64_t now_us);
        /** @brief Sample the achieved tick rate once per second and publish it.
         *  @p now_ms is the loop's uptime; on the simulator the
         *  window is measured in real time instead, so the rate stays
         *  meaningful under the simulator's manual clock. */
        void _record_tps(uint64_t now_ms);
        /** @brief Close the work window: record the work duration and, at slice
         *  boundaries, ask the HAL for the stack high-water. Publish both. */
        void _sample_work();
        /** @brief Fold one HAL::Stack::high_water() reading into the all-time
         *  peak and publish the usage fraction and the worst-case headroom. */
        void _measure_stack_usage();

        /** @} */
    };

#ifdef TARGET_SIMULATOR
    /**
     * @brief Best-effort: lock all process memory (current + future) into RAM
     *        to avoid page-fault latency. Sim-only (POSIX), process-wide via
     *        mlockall(), call once.
     *
     * Spawn the emulator thread *before* this: arming MCL_FUTURE first makes
     * pthread_create lock the new thread's full stack and trip RLIMIT_MEMLOCK
     * (EAGAIN). On failure (usually lack of privileges) the process keeps
     * running and says so on stderr under `[sim-rt]`, never through the logger
     * (see _rt_say). Suppressible via BOTANIK_NO_RT_WARNING.
     */
    void lock_process_memory();

    /**
     * @brief Best-effort: switch the *calling thread* to SCHED_FIFO and pin it
     *        to a core. Sim-only (POSIX).
     *
     * Policy and affinity are per-thread on Linux, so call it from each thread
     * that needs realtime (main loop on one core, emulator on another,
     * mirroring the OPTA's cores). On failure the thread keeps running and says
     * so on stderr under `[sim-rt]`. Suppressible via BOTANIK_NO_RT_WARNING.
     *
     * @param cpu_core_id Core to pin the calling thread to.
     */
    void make_thread_realtime(int cpu_core_id);
#endif

} }

#endif // SCHEDULER_SCHEDULER_H
