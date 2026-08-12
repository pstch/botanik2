# The scan cycle {#dev_scan}

How the control programs are scheduled, what they are guaranteed to see, and what
control authority does to them. The site-facing view - writing a program, the
function blocks - is @ref int_tasks; this page is the cycle itself.

## Two axes: rate and phase

A task has two independent attributes, and conflating them is the mistake to
avoid.

**Rate** is how often it runs - its group. **Phase** is where in the beat it sits.
Neither derives from the other: `expire_stale` is background-rate and
input-phase; a control program is whatever rate and always execute-phase. A
program names a rate GROUP like anything else in the beat - it has no private
period, so retuning a rate class retunes it too.

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

Six phases run in this order, every beat: INPUT, CONFIG, SERVICE, EXECUTE,
OUTPUT, PRESENT, then the watchdog kick.

@image html beat.drawio.svg "One beat. The number on a task is its group period in ms; a task runs only on the beats its group has come due, so most beats are far emptier than this. Only the gated site hook is drawn - the five ungated ones sit at the end of their own phases."

Within a phase, tasks run in registration order; a task runs only on the beats its
group has come due (@ref dev_scheduler). The sandwich is **read, execute, write**,
and it holds on every beat regardless of which programs happen to be due: whatever
is due runs between this beat's input phase and this beat's output phase.

The dashed box is the control-authority gate - the program entries and
`site_control`, but not the ungated site hooks; *Where the gate lives* below is
the whole rule.
Note also that `modbus_tcp_wr` sits in SERVICE although it is registered beside
its TCP siblings much further down the function: a task declares its phase, so
where its line falls in `_register_tasks()` says nothing about when it runs.

@note The diagram is `images/beat.drawio.svg`, an SVG that doubles as its own
draw.io document - open it in draw.io or the VS Code extension and save, and it
re-renders in place. Every diagram under `images/` works this way; see
[Diagrams](@ref dev_build) for the convention.

## What a program is guaranteed

- **Table order is execution order.** The programs run in the order the site
  declared them - not alphabetically, not by rate, not by construction order.
  They register in table order, and within a phase the registry dispatches in
  registration order, so the site's array IS the order.
  So a law that computes a setpoint and a law that acts on it complete in *one*
  beat when written in that order, and take a beat per link when written the
  other way. That is a contract sites rely on, so `test_plc_io.cpp` pins it both
  ways round: the same three programs in two orders must produce two orders.
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

`Vars::Bank::control_enabled()` gates the control phase and nothing else. The
phases around it must **not** be gated with it: withholding control writes safe
states into the image, and something still has to carry them outward.

**Only the return edge does anything**, and it is one call:
`Tasks::Registry::set_programs_enabled()`, made from the `control_latch` task in
SERVICE - before CONTROL, so a withdrawal takes effect on the beat it is decided
rather than the next one.

| Edge | What happens | Why |
|---|---|---|
| withdrawn | the gate shuts, and nothing else | a program that is not called cannot act on what it remembers, and nothing outside it reads that |
| returned | the gate opens, every program's **timer is reseeded**, and `Tick::first` is raised on each | a `Ton` holding 5 s handed an hour of `dt` fires the instant control returns - the one thing a withdrawal exists to prevent. The flag is the other half: it says the process moved without us, which `dt` alone cannot express |

There is no `reset()` and no `resume()` on a program: `Context::first_scan` is
the whole signal, and the program drops whatever it judges stale under it. That
is not merely equivalent to a hook on the withdrawal edge - it is better placed.
The reseed means the program does **not** run on the beat authority returned, so
an edge hook would look at an image its own next `run()` never sees.

Clearing under the flag is opt-in per program and deliberately not "clear
everything": a dwell timer is stale, but a totalizer holding the litres delivered
today is a record.

`Context::first_scan` is IEC 61131-3's first-scan flag. A law that ramps, latches
or compares against "last time" has no last time on the first scan, and reading
one gives it whatever a member was initialised with. It is true at bring-up and
true again after a withdrawal, because those are the same situation: the process
moved without us, so the previous activation describes a plant that no longer
exists.

Bring-up therefore needs no special case at all - which is the argument for the
flag over a pair of hooks. Control starts withheld and is granted before the
first beat, so bring-up takes the return edge; and a task's first activation ever
raises `first` anyway, so the two arrive at the same place by two routes.

## Where the gate lives

Per **dispatch point**, not per write. `Vars::Var::set()` is not gated: the
handle is two pointers passed by value, made on the fly and passed everywhere,
and it has no route to the latch - giving it one would cost eight bytes on the
hot path to re-answer a question the caller already knew.

So the gate sits wherever a writer is *dispatched*, and there are exactly two:

| Dispatch point | Hook | Gated |
|---|---|---|
| `Tasks::Registry::run_phase` on a program entry | each `Plc::Program::run` | yes |
| the `site_control` task | `BotanikFirmware::on_control` | yes |
| the other five `site_*` tasks | `on_input` / `on_config` / `on_service` / `on_output` / `on_present` | **no** - by design |

The ungated five are for work that is still correct while the plant is stopped:
bookkeeping, derived values, telemetry. Anything that DRIVES the field belongs
in `on_control()`, or it keeps driving through a health fault. It runs in its own
phase, so `on_output()` and `on_present()` see the same beat's decisions.

That list is exhaustive by construction: a third thing that drives the field has
no gate unless it comes here and adds one. Which is the point - it is a short
list that has to be read, rather than a rule spread over every write.

See @ref user_control for the operator's view, and @ref dev_architecture for where
in the loop this sits.
