# The task table {#dev_tasks}

What runs in a beat, at what rate, and in what order. `LoopScheduler`
(@ref dev_scheduler) decides *when* the beat happens; this decides *what runs in
it*, which is why `tasks/` sits below the scheduler and references none of it.

Each entry names what runs, the rate **group** it belongs to, and the **phase**
it sits in; `Tasks::Registry` (`tasks/tasks.h`) holds them and
`BotanikFirmware::loop()` dispatches phase-major. Which task goes where is
`_register_tasks()`, and it reads top to bottom as the beat does.

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
| `fast` | `TASK_PERIOD_MS` (10 ms) | the field bus, the serial pumps, the log drain |
| `main` | 50 ms | the operator-facing rate: the console render |
| `slow` | 100 ms | dwell and config: health, parameters |
| `background` | 500 ms | housekeeping measured in seconds: the expiry sweep |

## What sets a rate

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

## The rates

Why each task sits in the group it does. "Before" is the flat 10 ms every task
paid when the loop was a fixed call sequence.

| Task | Phase | Group | Binding constraint | Cut |
|---|---|---|---|---|
| `sys_console_rx` | input | fast | 4800 baud fills the 255-byte ring in 585 ms; echo latency | - |
| `sys_expire_stale` | input | background | timeouts are 3x a poll group's period, so 3 - 180 s | 50x |
| `sys_params` | config | slow | human edit latency | 10x |
| `sys_health` | service | slow | alarm dwell (`on_s`/`off_s`) is in seconds | 10x |
| `sys_control_latch` | service | fast | must see a health fault before the programs run | - |
| `sys_net` | service | fast | MQTT keepalive 60 s, telemetry 30 s; TCP drain | - |
| each control program | execute | its own | the process time constant it acts on | - |
| `site` | execute | fast | the site's own per-beat hook | - |
| `logger` | present | fast | queue drain against the boot burst; eviction risk | - |
| `sys_console` | present | slow | 4800 baud = 4.4 bytes/beat; echo perception | 10x |
| `sys_monitor_log` | present | fast | log queue drain, and the port either way | - |
| `sys_console_tx` | present | fast | reconcile the frame, keep the UART fed | - |
| *Modbus commit / collect* | input / output | fast | ring fills in 146 ms @19200, **24 ms** @115200 | - |

The two wins are `sys_expire_stale` (50x) and the console render (10x, and the most
expensive item per invocation). `logger`, the serial pumps and the watchdog kick
stay on the beat deliberately: they are safety- or latency-critical.

A control program is a row here like any other. It names a rate GROUP rather than
a private period, so it is spread by the same derived offset and reported by the
same per-task cost - there is no longer one `scan` row standing for all of them.

The port pumps are no longer rows of their own. `BufferedSerial` is a ring buffer
and a port, not a `Component`: `console_in` and `console_tx` are the `Console`'s
own tasks and take the bytes in and out at the ends of the beat, and
`monitor_log` is the whole monitor port in one, since nothing reads what it
receives.

The watchdog kick is not in the table. It stays inline at the very bottom of
`loop()`, after every phase, because it must reflect that this beat completed -
a kick that could itself be rated is a kick that can outlive the work it vouches
for.

## Three couplings a period alone does not fix

**The terminal's TX pacing is a ring DEPTH FLOOR, not a per-tick allowance**
(@ref dev_screen). `present()` computes `budget = _tx_cold_depth - tx_used()`
(`console/terminal.cpp`), where `_tx_cold_depth` is `depth_for_baud(baud,
TX_COLD_TARGET_MS)` - 128 ms of wire time, 61 bytes at 4800 baud. So it tops the
ring back up to a fixed latency target however often it runs: period-independent,
and slowing the render does **not** cut throughput.

What it does impose is a ceiling, and splitting the draw from the flush is what
moved it off the render: **the period that must stay well under
`TX_COLD_TARGET_MS` is the FLUSH's**, since that is what calls `present()`. It is
`console_tx`, at the beat, against 128 ms. The draw only fills the FRONT grid,
so it is free to be as slow as the eye allows.

**`LOG_ROWS_PER_TICK` is genuinely per-tick**, and it is the one quantity that
does scale with the DRAW rate: 2 rows per tick is 200 rows/s at 10 ms and
20 rows/s at the 100 ms the draw actually runs at. Not binding at 4800 baud,
where the wire ceiling is ~4.4 lines/s and 20 stays 4.5x clear of it - but it
becomes the limit on a site that runs a fast console link, and it wants
rescaling there.

**Periods must be integer multiples of the beat.** A 100 ms task on a 30 ms beat
fires at 120 ms. Harmless while the beat is 10 ms and every period above divides
by it; sharp the moment the beat moves.

**`health` in the slow group makes `watchdog_ok()` up to 100 ms stale**, and the
loop reads it every beat. Harmless against a hardware watchdog measured in
seconds, but it should be a decision rather than a surprise.

## Phase offsets: rates cut the mean, offsets cut the peak

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

The control programs get exactly this treatment and no other: they are registry
entries, so `Registry::add()` spreads them across their group like everything
else. There is no separate scan stagger any more, and nothing for a site to set.

## Lowering the beat

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

## TaskTimer

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
