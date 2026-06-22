# Scheduler & Performance Monitoring

How the firmware runs everything from one non-blocking loop at a fixed period,
and how it measures its own CPU load, scheduling jitter, stack usage, and
achieved tick rate.

## The loop

`LoopScheduler` (`scheduler/scheduler.h`) drives a fixed-period loop without ever
blocking the RTOS. The main loop calls `tick()` continuously; it returns `true`
exactly once per period, when the work for that beat should run.

```
 while (true) {
     if (scheduler.tick()) {     // true once per TASK_PERIOD_MS
         ...do the work...        // serial, PLC sync, logging, terminal
     }
 }
```

Between grid points `tick()` sleeps until ~1 ms before the next one, then
busy-waits the last millisecond to release the beat with low jitter (a
class-scope `static_assert(TASK_PERIOD_MS >= 2)` keeps that sleep/spin split
valid against a future retune). Nothing in the work path blocks, so a stalled
subsystem can't wedge the loop (and on the OPTA, trip the hardware watchdog).

## Overrun handling

If the work for a beat runs long, the loop misses one or more grid points.
`_skip_beats()` advances the next-tick anchor past every missed beat (so the loop
doesn't try to "catch up" with a burst of back-to-back ticks), counts them in
`SchedulerContext::overruns`, and logs an aggregated `"scheduler overrun: skipped
N beat(s)"` warning at most once per `SCHEDULER_XRUN_LOG_MS`.

```
 grid:   │────────│────────│────────│────────│   period = TASK_PERIOD_MS
         t0       t1       t2       t3       t4
 work:   ▓▓▓▓               ▓▓▓▓▓▓▓▓▓▓▓▓▓▓
         tick t0            tick t1  └ ran past t2,t3 ┘
                            overruns += 2, anchor jumps to t4
```

## Metrics

`LoopScheduler` instruments itself across each beat - it's the only thing that
knows the loop's timing - keeping four metric families (CPU load, loop time,
jitter, stack usage), each exposed raw, smoothed (EMA), and as a recent windowed
peak, plus a plain **ticks-per-second** rate.

```
 tick (release) ───▶ ...work runs... ───▶ tick (next release)
        │                                     │
        │ on release:                         │ at the next tick:
        ├─ tick lateness ───▶ jitter          ├─ work duration ─▶ loop time
        └─ period − slept ──▶ CPU load        └─ stack high-water ─▶ stack use
```

- **CPU load** - the *duty cycle*: the active fraction of each period,
  `(period − slept)/period`. It counts the work, the loop/scheduler overhead, and
  the ~1 ms busy-wait (everything but the real sleep), so it reflects total CPU
  per beat, not just the explicit work.
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
  second (`HAL::real_uptime_ms()`), so it stays truthful when `BOTANIK_TIME_SCALE`
  dilates sim time. A single value (no EMA/peak).

Two filters back the "smoothed / recent" columns: `ExpMovingAverage` (asymmetric
attack/decay EMA) and `WindowedMax<K>` (O(1)-per-sample sliding-window maximum over K
rotating buckets), both in `utils/filters.h`. All tuning constants live in
`libbotanik_config.h`.

## Publishing

`PerfContext` holds the CPU-load / loop-time / stack fields; `SchedulerContext`
inherits it and adds jitter plus `tick_count`, `overruns`, `ticks_per_second`, and the
shared per-tick `timestamp_ms`. The scheduler writes every field in place (no copy
step); consumers (status bar, `stats` command, PLC sync timing) all read this one
struct via `LoopScheduler::context()`.

\startuml "SchedulerContext: the metrics struct, read via LoopScheduler::context()"
class PerfContext {
  cpu_load_ema : float - duty cycle (active fraction of period)
  cpu_loop_us_recent : uint32 - windowed peak work duration (us)
  cpu_loop_us_max : uint32 - all-time peak work duration (us)
  stack_usage : float - peak stack used / depth
  stack_bytes_free : uint32 - worst-case free stack (bytes)
  stack_depth : uint32 - total stack allocation (bytes)
  ticks_per_second : uint32 - achieved loop rate
}
class SchedulerContext {
  cpu_jitter_ema : float - smoothed tick jitter
  cpu_jitter_us_recent : uint32 - windowed peak jitter (us)
  cpu_jitter_us_max : uint32 - all-time peak jitter (us)
  tick_count : uint32
  overruns : uint32 - beats skipped on overrun
  timestamp_ms : uint32 - shared per-tick now (wraps ~49 days)
}
PerfContext <|-- SchedulerContext
\enduml
