# The scan cycle {#dev_scan}

How the control programs are scheduled, what they are guaranteed to see, and what
control authority does to them. The site-facing view - writing a program, the
function blocks - is @ref int_tasks; this page is the cycle itself.

## Two axes: rate and phase

A task has two independent attributes, and conflating them is the mistake to
avoid.

**Rate** is how often it runs - its group. **Phase** is where in the beat it sits.
Neither derives from the other: `expire_stale` is background-rate and
input-phase; a control program is whatever rate and always execute-phase.

Rate is a tuning knob. Phase is a correctness property - it encodes the data flow,
and getting it wrong corrupts a cycle rather than just costing CPU. So the phase
order stays written out as code in `firmware/firmware.h`, and only the rate is a
property of the task. A group must never imply an order, or retuning a rate would
silently reorder the cycle.

There is deliberately **no priority**. Priority in a PLC means preemption, and
there is none here: one cooperative loop, every step runs to completion
(@ref dev_concurrency). Ordering is the phase; the only meaningful "priority"
would be load shedding, and shedding a control program is not something that
should ever be automatic.

## The beat

Six phases, in this order, every beat:

```
 INPUT  ─▶ CONFIG ─▶ SERVICE ─▶ EXECUTE ─▶ OUTPUT ─▶ PRESENT ─▶ kick
 serial    params    health     control    (field    logger
 expire              net        programs    write)   console/TX
                                gated by
                                control_enabled()
```

Within a phase, tasks run in registration order; a task runs only on the beats its
group has come due (@ref dev_scheduler). The sandwich is **read, execute, write**,
and it holds on every beat regardless of which programs happen to be due: whatever
is due runs between this beat's input phase and this beat's output phase.

## What a program is guaranteed

- **The image is stable within a beat.** It mutates only between phases, never
  under a running program, because every writer is cooperative and runs to
  completion. Two programs due on the same beat agree about what they read.
- **`dt_ms` is the real elapsed time**, never the nominal period, and a program
  due late runs *once* - missed activations are dropped, not caught up. A burst of
  catch-up runs would land exactly when the system is already behind.
- **Staleness is explicit, not inferred.** A row past its timeout goes invalid
  (`Bank::expire_stale()`), so logic reads quality rather than guessing from the
  value. A row whose reading has left its declared band goes invalid too
  (`reject_implausible()`) - a dead 4-20 mA loop reads full scale, and no timeout
  catches that.

Rows carry independent ages, and nothing pretends otherwise: each has its own
`stamp_ms` and `valid`. Snapshotting the image into a per-cycle shadow would
restore "one instant" only in appearance - it would make the values *stable*,
which they already are, without making them *contemporaneous*.

## Control authority, and its two edges

`Vars::Bank::control_enabled()` gates the execute phase and nothing else. The
phases around it must **not** be gated with it: `disable_control()` writes safe
states into the image, and something still has to carry them outward.

Both edges reach the programs (`Scan::Driver::on_step`):

| Edge | What the driver does | Why |
|---|---|---|
| withdrawn | `reset()` on every program, once | from here the process moves without us, so what a program remembered about the field is stale |
| returned | reseeds every program's timer | a `Ton` holding 5 s handed an hour of `dt` fires the instant control returns - the one thing a withdrawal exists to prevent |

`reset()` is opt-in per program and deliberately not "clear everything": a dwell
timer is stale, but a totalizer holding the litres delivered today is a record.

## Where the gate lives

Per **dispatch point**, not per write. `Vars::Var::set()` is not gated: the
handle is two pointers passed by value, made on the fly and passed everywhere,
and it has no route to the latch - giving it one would cost eight bytes on the
hot path to re-answer a question the caller already knew.

So the gate sits wherever a writer is *dispatched*, and there are exactly two:

| Dispatch point | Hook | Gated |
|---|---|---|
| `Scan::Driver::on_step` | each `Plc::Program::run` | yes |
| the `site_ctl` task | `BotanikFirmware::on_control` | yes |
| the `site` task | `BotanikFirmware::on_step` | **no** - by design |

`on_step()` is for work that is still correct while the plant is stopped:
bookkeeping, derived values, telemetry. Anything that DRIVES the field belongs
in `on_control()`, or it keeps driving through a health fault. `on_control()`
runs first, so `on_step()` sees the same beat's decisions.

That list is exhaustive by construction: a third thing that drives the field has
no gate unless it comes here and adds one. Which is the point - it is a short
list that has to be read, rather than a rule spread over every write.

See @ref user_control for the operator's view, and @ref dev_architecture for where
in the loop this sits.
