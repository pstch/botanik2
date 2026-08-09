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

The beat is a **task table**. Each entry names what runs, the rate **group** it
belongs to, and the **phase** of the beat it sits in; `Tasks::Registry`
(`tasks/tasks.h`) holds them and `BotanikFirmware::loop()` dispatches
phase-major. Which task goes where is `_register_tasks()`, and it reads top to
bottom as the beat does.

Rate and phase are independent, and conflating them is the mistake to avoid. Rate
is a tuning knob; phase is a correctness property, because it encodes the data
flow (@ref dev_scan). So the phase order stays written out as code and only the
rate is a property of the task - a group that implied an order would mean
retuning a rate silently reordered the cycle.

There is deliberately **no priority**. Priority means preemption and there is
none here: one cooperative loop, every step runs to completion
(@ref dev_concurrency). The only meaningful reading would be a load-shedding
policy, and shedding a control program is not something that should happen
automatically.

| Group | Period | For |
|---|---|---|
| `fast` | `TASK_PERIOD_MS` (10 ms) | the field bus, the serial pumps, the log drain, the scan driver |
| `main` | 50 ms | the operator-facing rate: the console render |
| `slow` | 100 ms | dwell and config: health, parameters |
| `background` | 500 ms | housekeeping measured in seconds: the expiry sweep |

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

Why each task sits in the group it does. "Before" is the flat 10 ms every task
paid when the loop was a fixed call sequence.

| Task | Phase | Group | Binding constraint | Cut |
|---|---|---|---|---|
| `console_rx` | input | fast | 4800 baud fills the 255-byte ring in 585 ms; echo latency | - |
| `monitor_rx` | input | fast | 115200 fills the ring in **24 ms** | - |
| `expire_stale` | input | background | timeouts are 3x a poll group's period, so 3 - 180 s | 50x |
| `params` | config | slow | human edit latency | 10x |
| `health` | service | slow | alarm dwell (`on_s`/`off_s`) is in seconds | 10x |
| `control_latch` | service | fast | must see a health fault before the programs run | - |
| `net` | service | fast | MQTT keepalive 60 s, telemetry 30 s; TCP drain | - |
| `scan` | execute | fast | must **divide** the fastest control program (100 ms) | - |
| `site` | execute | fast | the site's own per-beat hook | - |
| `logger` | present | fast | queue drain against the boot burst; eviction risk | - |
| `console` | present | main | 4800 baud = 4.4 bytes/beat; echo perception | 5x |
| `monitor_log` | present | fast | log queue drain | - |
| `console_tx` / `monitor_tx` | present | fast | keep the UART fed | - |
| *Modbus commit / collect* | input / output | fast | ring fills in 146 ms @19200, **24 ms** @115200 | - |

The two wins are `expire_stale` (50x) and the console render (5x, and the most
expensive item per invocation). `scan`, `logger`, the serial pumps and the
watchdog kick stay on the beat deliberately: the scan's not-due path is two
comparisons per program, and the others are safety- or latency-critical.

The watchdog kick is not in the table. It stays inline at the very bottom of
`loop()`, after every phase, because it must reflect that this beat completed -
a kick that could itself be rated is a kick that can outlive the work it vouches
for.

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

**`health` in the slow group makes `watchdog_ok()` up to 100 ms stale**, and the
loop reads it every beat. Harmless against a hardware watchdog measured in
seconds, but it should be a decision rather than a surprise.

### Phase offsets: rates cut the mean, offsets cut the peak

A period alone buys less than it looks. Every component first runs on the first
beat and seeds its timer from it, so **unoffset periods stay phase-locked to boot
forever**: params and health, both 100 ms, come due on the same beat every time,
the console joins them every 100 ms, and the expiry sweep and the site's 1000 ms
control program join every second.

So the rates above cut the **mean** work per beat and leave the **peak** exactly
where it was - one beat that still does the lot. Which matters, because the peak
is what `cpu_loop_us_max` records and what decides the shortest beat the loop can
hold. Rates alone buy CPU; they buy no headroom.

`TaskTimer::set_offset()` fixes it, as a one-shot delay on the first interval
only: a task offset by 30 ms waits `period + 30` for its second activation and
the nominal period thereafter, so it runs permanently out of step with an
unoffset task of the same period.

The offsets are **derived, not declared**. `Registry::add()` counts how many
members a group already has and places each new one a beat further along, wrapped
into the group's period. A site writes nothing, and adding a task re-spreads the
group. When a group has more members than its period has beats they start to
share slots again, which is no worse than the phase-locked default.

Retune nothing here; retune the group's period and the spread follows. The first
beat is the one unavoidable pile-up: a task's first activation is what runs its
`setup()`, so nothing can be deferred past it.

The control programs get the same treatment, but derived rather than declared:
`Scan::Driver::setup()` offsets task *i* by `i * SCAN_STAGGER_MS` (one beat by
default), wrapped into that task's own period. A site writes nothing, and adding
a program re-spreads the table. An every-beat program (`period_ms` 0) is left
alone - it has no interval to shift inside. `SCAN_STAGGER_MS = 0` disables it
outright, for a site whose programs must share a beat.

### Lowering the beat

Sub-10 ms dispatch is not a task-period feature: a 10 ms beat cannot release
anything at 4 ms. It needs `TASK_PERIOD_MS` itself lowered, gated on three
things.

1. **Periods AND offsets must land first.** Dropping 10 ms to 2 ms multiplies
   every component's work by 5, which the rates undo - but only the offsets bring
   the peak down, and it is the peak that has to fit inside the shorter beat.
2. **The beat can never be shorter than the worst-case single-beat work.** Read
   it from `stats` before choosing a period, and size against
   `cpu_loop_us_recent` (the windowed peak) rather than `cpu_loop_us_max`: the
   all-time max never decays, so one outlier - a debugger pause, a preempted
   sim - pins it for the life of the boot. Use `max` as the separate "has this
   ever gone catastrophically long" check.
3. **The sleep/spin split has to go to microseconds.** `tick()` busy-waits the
   last ~1 ms, so a 2 ms beat would spend 50 % of the CPU spinning against 10 %
   today - which is what `static_assert(TASK_PERIOD_MS >= 2)` is there to flag.

The only sub-beat consumer identified so far is a Modbus link at 115200 wanting
5 ms; at 19200 the RX ring gives 146 ms of slack and needs nothing. Greenhouse
physics asks for nothing below 100 ms.

### TaskTimer

The due/late/dropped decision is one rule, shared rather than reimplemented per
call site. `Tasks::TaskTimer` (`tasks/task_timer.h`) holds the two fields
of per-task state and answers it:

- **Due** when `period_ms` is 0 (every beat), on the first ever call, or once
  `now_ms - last_run_ms >= period_ms`.
- **`dt_ms` is the ACTUAL elapsed time** since that task last ran, never the
  nominal period, so a task that ran late does not lose the time it was late by.
  The first run reports the nominal period instead - handing a timer "everything
  since boot" would fire it immediately.
- **`missed` counts dropped activations.** A task due late runs ONCE; missed
  activations are dropped, never caught up, because a burst of catch-up runs
  lands exactly when the system is already behind. `Registry::run_phase()`
  accumulates them into `TaskCost::missed`, which the TASKS page and `tasks`
  report - the drop leaves no other trace, so a task quietly running at half its
  declared rate would otherwise look exactly like one keeping up.

`due()` is a pure query and `ran()` commits, so a caller that decides not to run
(the scan while control is withheld) does not silently consume the activation.
