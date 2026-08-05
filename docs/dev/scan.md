# The scan cycle {#dev_scan}

How the control programs are scheduled, what they are guaranteed to see, and
where the field bus meets them. The site-facing view - writing a program, the
function blocks - is @ref int_tasks; this page is the cycle itself.

Written ahead of the code: the rate groups, the phase split and the Modbus
commit/collect boundary below are the design, not yet the implementation. What
exists today is `Scan::Driver` running per-program periods once per beat.

## The problem a fieldbus creates

The textbook PLC cycle is read inputs, execute, write outputs, once per cycle,
with the I/O update synchronous at the cycle boundary. That works when I/O is
backplane-local and costs microseconds.

A Modbus RTU sweep of this plant is **555 ms at 19200 baud** (@ref dev_modbus),
which is 55 beats. Inputs cannot be refreshed at the top of a 10 ms beat, and no
amount of tuning changes that - it is the wire.

So the cycle splits in two, the way every multi-rate PLC does once a fieldbus is
involved. The bus runs on its own clock and maintains a **process image**; the
cycle reads that image rather than the field.

\dot "The bus and the cycle meet only at the image"
digraph scan_image {
  rankdir=LR; node [shape=box, fontsize=10]; edge [fontsize=9];
  bus   [label="Modbus client\npoll groups, 1 txn in flight"];
  bank  [label="Vars::Bank\nprocess image", shape=box3d];
  prog  [label="control programs\nexecute phase"];
  bus  -> bank [label="commit (receive)"];
  bank -> prog [label="raw() / valid()"];
  prog -> bank [label="demands"];
  bank -> bus  [label="collect (write on change)"];
}
\enddot

## Two axes: rate and phase

A task has two independent attributes, and conflating them is the mistake to
avoid.

**Rate** is how often it runs - its group. **Phase** is where in the beat it sits.
Neither derives from the other: the Modbus client is fast-rate *and* split across
two phases; `expire_stale` is background-rate *and* input-phase; a control program
is whatever rate *and* always execute-phase.

Rate is a tuning knob. Phase is a correctness property - it encodes the data flow,
and getting it wrong corrupts a cycle rather than just costing CPU. So the phase
order stays written out as code in `core/firmware.h`, and only the rate is a
property of the task. A group must never be allowed to imply an order, or
retuning a rate would silently reorder the cycle.

There is deliberately **no priority**. Priority in a PLC means preemption, and
there is none here: one cooperative loop, every step runs to completion
(@ref dev_concurrency). Ordering is the phase; the only meaningful "priority"
would be a load-shedding policy, and shedding a control program is not something
that should ever be automatic.

## The beat

```
 RX ─▶ modbus.receive ─▶ expire_stale ─▶ params ─▶ health ─▶ net
                                                              │
        ┌─────────────────────────────────────────────────────┘
        ▼
    [ CONTROL PROGRAMS ]  ─▶ collect ─▶ modbus.transmit ─▶ logger ─▶ render ─▶ TX ─▶ kick
        gated by                dirty      next request
        control_enabled()       outputs
```

The sandwich is **commit, execute, collect**, and it holds on every beat
regardless of which programs happen to be due. Programs at 100 ms and 1000 ms do
not complicate it: whatever is due on a beat runs between that beat's commit and
that beat's collect.

**The client is split like the serial pump.** `BufferedSerial` is one Component
whose two halves the loop drives at the phases they belong to - RX at the top, TX
after the render. The Modbus client takes the same shape for the same reason:
`receive()` parses completed responses and commits them, so a program sees the
freshest possible image; `transmit()` issues the next request, so a demand written
this beat reaches the queue without waiting for the next one. Stepping it once,
at either end, costs a beat of latency in one direction or the other.

## What a program is guaranteed

The current contract in `scan/scan.h` promises that inputs are refreshed once per
beat before any task runs, so every task in a beat sees the same instant. **With
an asynchronous bus that is no longer true** and the contract has to be restated.
What survives is what actually matters:

- **The image is stable within a beat.** It mutates only between beats, never
  during one, because every writer is cooperative and runs to completion. So no
  program can see a value change under it mid-execution, and two programs on the
  same beat agree.
- **Rows carry independent ages.** A grid reading polled at 1 s and a humidity
  reading polled at 10 s are not from the same instant, and nothing pretends
  otherwise. Each row carries its own `stamp_ms` and `valid`.
- **Staleness is explicit, not inferred.** A row past its timeout goes invalid
  (`Bank::expire_stale()`), so logic reads quality rather than guessing from the
  value. Declare each row's timeout at ~3x its poll group's period.

That is what every multi-rate PLC actually provides. Snapshotting the image into
a per-cycle shadow would restore "one instant" only in appearance - it makes the
values *stable*, which they already are, without making them *contemporaneous*,
which no bus can.

## Collect, and why it is not gated

`disable_control()` drops every `ROLE_OUTPUT` row to its declared safe state and
inhibits the execute phase. **Collect must still run.** Those safe values are in
the image and have not reached the field until a transaction carries them; gating
the collector alongside the programs would leave the actuators energised at
exactly the moment control was withdrawn.

So the gate covers execute only. Collect and `modbus.transmit()` run
unconditionally, and a safe-state write should be queued ahead of ordinary
traffic.

**The reset path needs the slave's help.** Control is withdrawn before the loop
stops kicking the watchdog, precisely so the field is safe before a reset. With
local I/O that is instantaneous. Over Modbus it is a transaction - ~58 ms for an
FC16 of 32 registers at 19200 - and a reset can easily beat it to the wire.

Do not try to win that race. Configure the **slave-side failsafe**: an output
module that has heard no valid command within its own timeout drops its outputs
itself. That is the standard industrial answer, it covers a severed cable and a
wedged master as well as a reset, and it is the only mechanism that works when
the master is the thing that failed. The master's safe-state write is the fast
path; the slave's failsafe is the guarantee.

## Write on change

Outputs are not polled. The collector walks the `ROLE_OUTPUT` rows, coalesces a
slave's dirty registers into one FC16, and holds a minimum interval between
writes (200 - 500 ms). A periodic unconditional rewrite (5 - 10 s) re-drives a
slave that rebooted, and doubles as the keepalive its failsafe timer is watching
for.

Dirt is marked in the `Value` on write rather than diffed against a shadow. The
struct is pinned at 32 bytes by a `static_assert` and adding a field costs 8 bytes
across the whole table - but there is a `uint8_t _rsv[3]` already in there, so the
flag is free.

**Collect must run at least as often as the fastest control program.** Otherwise a
demand can be superseded before it is ever collected. That is harmless for
`ROLE_OUTPUT` rows, which are level demands where last-value-wins is correct, but
it would quietly break anything edge- or pulse-shaped - so it is a contract, not
an accident to be relied on.

## Poll groups are tasks

A Modbus poll group is a name, a period, and some work - the same shape as
everything else on the beat. `modbus_grid_live` at 1 s, `modbus_sensors` at 10 s.
An activation enqueues a transaction; the client's state machine drains the queue
from the fast group. One scheduler, not a second one bolted alongside.

The block planning (which registers coalesce into which transaction, and the
17-register / 68-register merge thresholds) is in @ref dev_modbus.

## Per-task CPU

The accounting unit is the **task**, not the group: "the slow group cost 3 ms"
does not say which of its five members to look at. Roll up to the group for the
summary.

`Scheduler::TaskCost` already holds exactly this (`us_last`, `us_max`,
`us_total`, `runs`, `us_mean()`); what is missing is a `Plc::Program` carrying
one, measured with two `HAL::Clock::uptime_us()` reads around `step()`. The
program already owns its timer, so the cost belongs beside it rather than in a
parallel array - which is also what makes a registry unnecessary here: the
program *is* the record, so there is no intrusive list and no static-init-order
hazard.

Report a windowed peak and a mean. An all-time max never decays, so one outlier
pins it for the life of the boot (see @ref dev_scheduler).
