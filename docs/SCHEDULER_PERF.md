# Scheduler & Performance Monitoring

How the firmware runs everything from one non-blocking loop at a fixed period,
and how it measures its own CPU load, scheduling jitter, and stack usage.

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

## Metrics

`LoopScheduler` instruments itself across each beat - it's the only thing that
knows the loop's timing - and keeps four metric families, each exposed raw,
smoothed (EMA), and as a recent windowed peak.

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
- **Jitter** - how late a tick was released against the time grid.
- **Stack usage** - measured by *painting* the free stack with `STACK_PAINT_BYTE`
  and later binary-searching for the high-water boundary the work dirtied.
  Painting/scanning happens once per slice (not every tick) to keep it cheap. The
  paint runs at the end of `tick()`, which is `always_inline` so it folds into the
  caller's frame and the painted ceiling sits just above the work that dirties it.

Two filters back the "smoothed / recent" columns: `ExpMovingAverage` (asymmetric
attack/decay EMA) and `WindowedMax<K>` (an O(1)-per-sample sliding-window maximum
over K rotating buckets). Both live in `utils/filters.h` as a small reusable layer.
All tuning constants - including the derived `LOAD_METRIC_WINDOW` and `STACK_SLICE_*`
- live in `libbotanik_config.h` (`*_EMA_*_MS`, `LOAD_METRIC_*`, `STACK_*`).

## Publishing

`PerfContext` (`scheduler/scheduler.h`) holds the CPU-load / loop-time / stack
fields; `SchedulerContext` *inherits* it and adds the jitter fields plus the
scheduler's `tick_count`, `overruns`, and the shared per-tick `timestamp_ms`. The
scheduler writes every field in place - there is no copy step. Consumers (the
terminal status bar, the `stats` shell command, the PLC sync timing) all read this
one struct via `LoopScheduler::context()`.

```
 LoopScheduler ──writes all metrics──▶  SchedulerContext  ──▶ status bar
                + tick_count / overruns / timestamp_ms        `stats` cmd
                (PerfContext base + scheduler fields)          PLC timing
```
