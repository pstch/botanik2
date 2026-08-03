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

## Multi-rate tasks

Today every component in the loop is stepped on every beat, at
`TASK_PERIOD_MS` = 10 ms. Most of them do not need it, and one of them (the
console) is oversampled by more than an order of magnitude. This section is the
budget for giving each one its own period; the mechanism is `Scheduler::TaskTimer`
(below), the rates are not wired up yet.

`Scan::Driver` (@ref int_tasks) is the exception that already works this way: a
control program declares a `period_ms` and runs when it comes due. The rest of
the loop should reach the same place.

### What sets a rate

Whichever of four constraints binds first:

| Constraint | Reads as |
|---|---|
| **Buffer** | a ring must be drained before it fills |
| **Perception** | a human must not notice the lag |
| **Physics** | the process time constant |
| **Protocol** | a dwell or timeout it has to resolve |

Anything faster than the binding constraint is waste. Two measurements carry most
of the argument:

- **`Bank::expire_stale()` is a full linear scan of all 259 rows**
  (`vars/model.cpp`), run every beat, to detect timeouts measured in *seconds*.
  About 25,900 row-visits per second to resolve a 3 s deadline.
- **The console link is 4800 baud.** It carries **4.4 bytes per 10 ms beat**. One
  80-column line is **206 ms** of wire time; a full 80x24 screen is **4.4 s**.
  Diffing a 1920-cell grid at 100 Hz to feed that is ~20x oversampled.

### The rates

| Task | Binding constraint | Now | Target | Cut |
|---|---|---|---|---|
| `console.serial().receive()` | 4800 baud fills the 255-byte ring in 585 ms; echo | 10 ms | 20 ms | 2x |
| `monitor.serial().receive()` | 115200 fills the ring in **24 ms** | 10 ms | 10 ms | - |
| `params.step()` | human edit latency | 10 ms | 100 ms | 10x |
| `io.expire_stale()` | timeouts are 3x group period, so 3 - 180 s | 10 ms | 250 ms | 25x |
| `health.step()` | alarm dwell (`on_s`/`off_s`) is in seconds | 10 ms | 100 ms | 10x |
| `net.step()` | MQTT keepalive 60 s, telemetry 30 s; TCP drain | 10 ms | 20 ms | 2x |
| `scan.step()` | must **divide** the fastest task period (100 ms) | 10 ms | 10 ms | - |
| `logger.step()` | queue drain against the boot burst; eviction risk | 10 ms | 10 ms | - |
| `console.step()` (render) | 4800 baud = 4.4 bytes/beat; echo perception | 10 ms | 50 ms | 5x |
| `console.serial().transmit()` | paired with the render | 10 ms | 50 ms | 5x |
| Watchdog kick | HW period is seconds, and it is the safety net | 10 ms | 10 ms | - |
| *Modbus client* (@ref dev_modbus) | ring fills in 146 ms @19200, **24 ms** @115200 | - | 10 ms | - |

The two wins are `expire_stale` (25x) and the console render (5x, and the most
expensive item per invocation). `scan`, `logger` and the watchdog kick stay on the
beat deliberately: the scan's not-due path is two comparisons per task, and the
other two are safety-critical.

### Three couplings a period alone does not fix

**The terminal's TX pacing is a ring DEPTH FLOOR, not a per-tick allowance**
(@ref dev_screen). `present()` computes `budget = _tx_cold_depth - tx_used()`
(`console/terminal.cpp`), where `_tx_cold_depth` is `depth_for_baud(baud,
TX_COLD_TARGET_MS)` - 128 ms of wire time, 61 bytes at 4800 baud. So it tops the
ring back up to a fixed latency target however often it runs: period-independent,
and slowing the render does **not** cut throughput.

What it does impose is a ceiling: **the render period must stay well under
`TX_COLD_TARGET_MS`**, or the ring drains empty between renders and the wire
idles. 50 ms against 128 ms leaves 2.5x of margin; anything past ~64 ms starts
eating it.

**`LOG_ROWS_PER_TICK` is genuinely per-tick**, and it is the one quantity that
does scale with the render rate: 2 rows per tick is 200 rows/s at 10 ms and
40 rows/s at 50 ms. Not binding at 4800 baud, where the wire ceiling is ~4.4
lines/s and 40 stays 9x clear of it - but it becomes the limit on a site that
runs a fast console link, and it wants rescaling there.

**Periods must be integer multiples of the beat.** A 100 ms task on a 30 ms beat
fires at 120 ms. Harmless while the beat is 10 ms and every period above divides
by it; sharp the moment the beat moves.

**`health.step()` at 100 ms makes `watchdog_ok()` up to 100 ms stale**, and the
loop reads it every beat. Harmless against a hardware watchdog measured in
seconds, but it should be a decision rather than a surprise.

### Lowering the beat

Sub-10 ms dispatch is not a task-period feature: a 10 ms beat cannot release
anything at 4 ms. It needs `TASK_PERIOD_MS` itself lowered, gated on three
things.

1. **Per-component periods must land first.** Dropping 10 ms to 2 ms multiplies
   every component's work by 5. Do it after the rates above are in force and the
   two roughly cancel.
2. **The beat can never be shorter than `cpu_loop_us_max`**, the worst-case
   single-beat work time. It is already measured and published; read it from
   `stats` before choosing a period.
3. **The sleep/spin split has to go to microseconds.** `tick()` busy-waits the
   last ~1 ms, so a 2 ms beat would spend 50 % of the CPU spinning against 10 %
   today - which is what `static_assert(TASK_PERIOD_MS >= 2)` is there to flag.

The only sub-beat consumer identified so far is a Modbus link at 115200 wanting
5 ms; at 19200 the RX ring gives 146 ms of slack and needs nothing. Greenhouse
physics asks for nothing below 100 ms.

### TaskTimer

The due/late/dropped decision is one rule, shared rather than reimplemented per
call site. `Scheduler::TaskTimer` (`scheduler/task_timer.h`) holds the two fields
of per-task state and answers it:

- **Due** when `period_ms` is 0 (every beat), on the first ever call, or once
  `now_ms - last_run_ms >= period_ms`.
- **`dt_ms` is the ACTUAL elapsed time** since that task last ran, never the
  nominal period, so a task that ran late does not lose the time it was late by.
  The first run reports the nominal period instead - handing a timer "everything
  since boot" would fire it immediately.
- **`missed` counts dropped activations.** A task due late runs ONCE; missed
  activations are dropped, never caught up, because a burst of catch-up runs
  lands exactly when the system is already behind.

`due()` is a pure query and `ran()` commits, so a caller that decides not to run
(the scan while control is withheld) does not silently consume the activation.
