# Scheduler & Performance Monitoring

How the firmware runs everything from one non-blocking loop at a fixed period,
and how it measures its own CPU load, scheduling jitter, and stack usage along
the way.

## The loop

`LoopScheduler` (`scheduler/scheduler.h`) drives a fixed-period loop without ever
blocking the RTOS. The main loop calls `tick()` continuously; it returns `true`
exactly once per period, when the work for that beat should run.

```
 while (true) {
     if (scheduler.tick()) {     // true once per TASK_PERIOD_MS
         scheduler.paint_stack(); // at user-code depth, before the work
         ...do the work...        // serial, PLC sync, logging, terminal
     }
 }
```

Between grid points `tick()` sleeps until ~1 ms before the next one, then
busy-waits the last millisecond so it releases the beat with low jitter. Nothing
in the work path blocks, so a stalled subsystem can't wedge the loop (and on the
OPTA, trip the hardware watchdog).

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

## Metrics — PerfMonitor

The scheduler owns a `PerfMonitor` (`utils/perf.h`) and drives it across each
beat. `PerfMonitor` keeps three families of metric, each exposed raw, smoothed,
and as a recent peak.

```
 work_begin() ───▶ ...work runs... ───▶ sample()
   (window open)                          (window closed)
        │                                     │
        ├─ busy time ────────▶ CPU load  (EMA, all-time max, windowed max)
        ├─ tick lateness ────▶ jitter    (EMA, all-time max, windowed max)
        └─ stack high-water ─▶ stack use (EMA, all-time max, windowed max)
```

- **CPU load** — busy time (work window duration) as a fraction of the period.
- **Jitter** — how late a tick was released against the time grid.
- **Stack usage** — measured by *painting* the free stack with `STACK_PAINT_BYTE`
  and later binary-searching for the high-water boundary the work dirtied.
  Painting/scanning happens once per slice (not every tick) to keep it cheap;
  this is why `paint_stack()` must be called inline at user-code depth.

Two filters back the "smoothed / recent" columns: `ExpMovingAverage` (asymmetric
attack/decay EMA) and `WindowedMax<K>` (an O(1)-per-sample sliding-window maximum
over K rotating buckets). Tuning constants live in `libbotanik_config.h`
(`*_EMA_*_MS`, `LOAD_METRIC_*`, `STACK_*`).

## Publishing

`PerfMonitor`'s `PerfSnapshot` is the metric payload; `SchedulerContext` *inherits*
it and adds the scheduler's own `tick_count`, `overruns`, and the shared per-tick
`timestamp_ms`. Consumers (the terminal status bar, the `stats` shell command, the
PLC sync timing) all read this one struct via `LoopScheduler::context()`.

```
 PerfMonitor::snapshot()  ──published into──▶  SchedulerContext  ──▶ status bar
 (cpu/jitter/stack)                            + tick_count           `stats` cmd
                                               + overruns             PLC timing
                                               + timestamp_ms
```
