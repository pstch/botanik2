# Scheduler and performance monitoring {#dev_scheduler}

How the firmware runs everything from one non-blocking loop at a fixed period,
and how it measures its own CPU load, scheduling jitter, stack usage, and
achieved tick rate. What runs inside a beat, and in what order, is in
@ref dev_architecture.

## The loop

`LoopScheduler` (`scheduler/scheduler.h`) drives a fixed-period loop without ever
blocking the RTOS. The main loop calls `tick()` continuously; it returns `true`
exactly once per period, when the work for that beat should run.

```
 while (true) {
     if (scheduler.tick()) {     // true once per TASK_PERIOD_MS
         ...do the work...        // serial, scan, logging, console
     }
 }
```

Between grid points `tick()` sleeps the whole way to the next one and returns
false, so every pass that is not a beat gives the CPU back. What that costs is
jitter: the beat is released no earlier than the platform's sleep resolves, and
on the OPTA that is the RTOS tick. Nothing in the work path blocks, so a stalled
subsystem can't wedge the loop (and on the OPTA, trip the hardware watchdog).

## Overrun handling

If the work for a beat runs long, the loop misses one or more grid points.
`_skip_beats()` advances the next-tick anchor past every missed beat (so the loop
doesn't try to "catch up" with a burst of back-to-back ticks), counts them in
`SchedulerContext::overruns`, and logs an aggregated `"scheduler overrun: skipped
N beat(s)"` warning at most once per `SCHEDULER_XRUN_LOG_MS`.

@image html overrun.drawio.svg "A beat whose work runs long. The loop misses t2 and t3; the anchor jumps straight to t4 rather than firing three ticks back to back."

## Metrics

`LoopScheduler` instruments itself across each beat - it's the only thing that
knows the loop's timing - keeping four metric families (CPU load, loop time,
jitter, stack usage), each exposed raw, smoothed (EMA), and as a recent windowed
peak, plus a plain **ticks-per-second** rate.

@image html metrics.drawio.svg "Where each family is sampled. Two come off the release, two off the next tick - so a full set costs one beat."

- **CPU load** - the *duty cycle*: the active fraction of each period,
  `(period − slept)/period`. It counts the work and the loop/scheduler overhead,
  so it reflects total CPU per beat rather than just the explicit work. Slept
  time is the MEASURED sleep, not the requested one, so a sleep that overshoots
  reads as jitter and not as load.
- **Loop time** - the work-window duration (release → next tick), i.e. the
  worst-case single-beat work time.
- **Jitter** - how late a tick was released against the time grid. The sample is
  clamped to zero if a tick ever releases ahead of grid, so an underflow can't
  poison the all-time/windowed max.
- **Stack usage** - measured by *painting* the free stack with `STACK_PAINT_BYTE` and
  binary-searching for the high-water boundary the work dirtied. Painting/scanning runs
  once per slice (not every tick) to keep it cheap; the paint runs at the end of
  `tick()`, which is `always_inline` so the painted ceiling sits just above the work
  that dirties it.
- **Ticks per second** - the achieved loop rate, counted over each elapsed *real*
  second (`HAL::real_uptime_ms()`), so it stays truthful under the simulator's
  manual clock, where sim time advances only when a harness says so. A single
  value (no EMA/peak).

Two filters back the "smoothed / recent" columns: `ExpMovingAverage` (asymmetric
attack/decay EMA) and `WindowedMax<K>` (O(1)-per-sample sliding-window maximum over K
rotating buckets), both in `utils/filters.h`. All tuning constants live in
`libbotanik_config.h`.

## Publishing

`SchedulerContext` holds the CPU-load / loop-time / stack fields, jitter, plus
`tick_count`, `overruns`, `ticks_per_second`, and the shared per-tick `timestamp_ms`.
The scheduler writes every field in place (no copy step); consumers (the status bar,
the `stats` command, the health monitor's load/jitter/stack alarms) all read this one
struct via `LoopScheduler::context()`.

\startuml "SchedulerContext: the metrics struct, read via LoopScheduler::context()"
class SchedulerContext {
  cpu_load_ema : float - duty cycle (active fraction of period)
  cpu_loop_us_recent : uint32 - windowed peak work duration (us)
  cpu_loop_us_max : uint32 - all-time peak work duration (us)
  stack_usage : float - peak stack used / depth
  stack_bytes_free : uint32 - worst-case free stack (bytes)
  stack_depth : uint32 - total stack allocation (bytes)
  ticks_per_second : uint32 - achieved loop rate
  cpu_jitter_ema : float - smoothed tick jitter
  cpu_jitter_us_recent : uint32 - windowed peak jitter (us)
  cpu_jitter_us_max : uint32 - all-time peak jitter (us)
  tick_count : uint32
  overruns : uint32 - beats skipped on overrun
  timestamp_ms : uint32 - shared per-tick now (wraps ~49 days)
}
\enduml

## See also

- @ref dev_tasks - the table the beat dispatches: rate groups, phase offsets,
  and `TaskTimer`'s due/late/dropped rule.
- @ref dev_scan - the control programs the EXECUTE phase runs.
