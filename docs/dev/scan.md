# The scan cycle {#dev_scan}

How the control programs are scheduled, what they are guaranteed to see, and what
control authority does to them. The site-facing view - writing a program, the
function blocks - is @ref int_tasks; this page is the cycle itself.

## Two axes: rate and phase

A task has two independent attributes, and conflating them is the mistake to
avoid.

**Rate** is how often it runs - its group. **Phase** is where in the beat it sits.
Neither derives from the other: `sys_expire_stale` is background-rate and
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
Note also that `sys_mbtcp_wr` sits in SERVICE although it is registered beside
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
  value. A row whose reading has left its declared band goes invalid too, judged
  as it arrives (`publish()`, or `Bank::publish_wire()` for a scaled row, whose
  band is in the calibration's parameters) - a dead 4-20 mA loop reads full
  scale, and no timeout catches that.
- **A program always writes its outputs**, every beat, whatever its inputs did.
  It runs the same law on whatever those inputs hold and says so on the way out:
  `Ref::set(v, current)` drives the value and drops the quality when `current`
  is false (`set(v, false)` is `hold(v)`). There is NO one-argument form, so a
  write that has not thought about quality does not compile. Skipping the write leaves a stale
  number reading `Q_GOOD`; substituting zero reads as a measurement, which is
  how a dead probe becomes a greenhouse at 0 degC. Two things sit outside this.
  An ACTUATOR goes to its declared safe state instead - a coil command is a
  decision, not an estimate, and there is no invalid position for a relay. An
  ALARM boolean cannot carry quality at all: `Vars::BankAlarms::raw()` reads the
  bool and never the qual, so the only policy it can express is that an
  unmeasured condition raises nothing.

Rows carry independent ages, and nothing pretends otherwise: each has its own
`stamp_ms` and `valid`. Snapshotting the image into a per-cycle shadow would
restore "one instant" only in appearance - it would make the values *stable*,
which they already are, without making them *contemporaneous*.

## How a program declares its variables

A program declares one typed member per row, in its own header, and resolves
them all in `bind()`:

```cpp
void SupplyWatch::bind(Vars::Bank& io) {
    ln     = io.resolve_in<float>("grid_LN_V");
    locked = io.resolve_out<bool>("sys_supply_lockout");
}
```

```cpp
Vars::RefIn<float>  ln;      // VAR_INPUT:  get() only
Vars::RefOut<bool>  locked;  // VAR_OUTPUT: this program is the one producer
```

The reference carries two facts the compiler then holds it to: the row's C++
type, and which IEC 61131-3 declaration section the program reads it through. A
`T` disagreeing with the row's declared type resolves to nothing, exactly as a
missing name does, and a row carrying text is refused outright - its value lives in the bank's pool,
where no typed reference reaches it.

A `Plc::Program` is a PROGRAM POU and takes no arguments, so its relationship to
the table is the configuration layer, not parameter passing. The standard splits
that three ways and so does `Vars::Dir`:

| `Dir` | 61131-3 | Meaning |
|---|---|---|
| `IN` | `VAR_INPUT`, `VAR_EXTERNAL CONSTANT` | the program may not write it; `drive()` does not compile |
| `OUT` | `VAR_OUTPUT`, `VAR_EXTERNAL` | this program is the row's one producer |
| `INOUT` | `VAR_IN_OUT` | read-modify-write, shared with other programs by declaration |

Two things follow from the direction being a type rather than a convention.

**A VAR_INPUT cannot be driven, and the error says so.** `drive()` static-asserts
on `Dir::IN`. The value is checked against the row's type rather than converted,
for the same reason `Scalar(int)` is deleted: `drive(a_u16_row, 3)` naming the
mismatch beats a deduction conflict naming nothing.

**Declaring an output claims it, at bind time.** A program's `bind()` runs
inside its own writer window, so `resolve_out()` claims the row there and then.
Two programs producing one row is therefore a *bring-up* failure that
`report_writer_conflicts()` names at the first health service, rather than
something noticed on whichever beat both first happened to write - which for a
row only a fault path drives could be months in. `drive()` still claims on every
write, cheaply (first claim wins, so it short-circuits), which covers a program
that reaches a row without declaring it.

**A shared row records no owner.** `resolve_inout()` marks the slot `VF_SHARED`,
and `claim_writer()` then returns without recording anyone. That is what
distinguishes a declared read-modify-write from the defect
`report_writer_conflicts()` exists to catch - two programs fighting over one
coil, which otherwise shows up only as an actuator chattering at beat rate.
Naming an owner for a shared row would report whichever program happened to
write last as *the* writer of a row that has none.

Binding is still by name at run time: `Bank::freeze()` may compact a redeclared
row out, so a reference fixed at compile time would be wrong by construction. A
name that resolves to nothing - or to a row of another type, which is as much a
wiring defect - lands on the inert sink and counts as unresolved, which
`tests/test_site.cpp` fails the gate on.

## Program health, and going safe

Every program answers for itself with a `Plc::Health`:

| | | |
|---|---|---|
| `HEALTHY` | 0 | normal operation, every output valid |
| `DEGRADED` | 1 | an input is invalid; the outputs are still driven |
| `FAULTED` | 2 | could not be wired at bring-up; never runs |
| `INHIBITED` | 3 | control stopped and actuators parked; still reporting |
| `PARKING` | 4 | control stopped, actuators still travelling |

The codes are published as a `T_ENUM` row, so the vocabulary may gain labels and
must never renumber them.

`HEALTHY` and `DEGRADED` are a severity pair and are ordered, so a reader takes
the worse of two and one comparison answers "is anything going wrong". The rest
are decided rather than compared.

**Health is re-derived every activation**, so a condition that clears is not
sticky. A program that never calls `degraded()` is one whose `run()` cannot fail,
and it reads healthy rather than unknown. `FAULTED` is the one exception, below.

### FAULTED is decided in setup, and only there

A program is `FAULTED` when it could not be **wired**: `bind()` named a row no
table declares, or a dependency it was handed is null. Nothing about that gets
better, so it is set once during setup and no activation revisits it.

Such a program is **never activated at all** - not `run()`, not `observe()`. Its
refs are inert sinks that read 0 for ever, so every value it published would be
a zero indistinguishable from a reading, and every alarm it derived would be a
quiet false negative. Better to run nothing and say why.

Two ways in. An unresolved name is caught by the base without the program
asking: `on_setup()` compares `Bank::unresolved_count()` across `bind()`. A null
dependency the program has to declare itself, with `fault()`, from `bind()`.

It counts as parked for the reboot handshake. A program that will never run can
neither travel nor arrive, and waiting on one would hold the plant out of its
reboot for ever.

An input that stops being believable is **not** this. That is `DEGRADED`, and it
is what a law says when it is short of something it wanted; `_watch_inputs()`
says it on the law's behalf for every row passed to `set_inputs()`, so a
program usually does not have to.

### INHIBITED is reached, not commanded

Parking is *work*. A valve has travel; a window has travel. So a request does not
assign the state:

1. `Registry::request_safe(true)` (or `request_disable()` on one program) raises
   the ask.
2. The program **keeps being activated** - one that is not run cannot close a
   valve. `run()` is called as always; what changes is the TARGET, which the law
   reads off `inhibit_requested()` and drives to instead of to a demand.
3. A law still travelling says so by calling `parking()` during that activation,
   and reads `PARKING`. One that does not is `INHIBITED`: a program with no
   actuators of its own is parked the moment it is asked, which needs no code.
   The travel must terminate.
4. `Registry::all_safe()` becomes true when every program has arrived.

`observe()` runs throughout both, so nothing stops being reported. That is the
whole point of naming the states for what STOPS: a plant going into its safe
state is exactly when someone is watching, and a status row that froze at the
moment of the request would be stale AND look live.

Only then does a supervisor act, and what it does next is the difference between
the two commands: a **reboot** drops control, forces every output to its declared
default and stops kicking the watchdog; a **halt** does the same but keeps kicking
it and stops the `LoopScheduler` running the cycle instead.

Dropping control the instant the operator asked would leave a half-travelled
actuator wherever the request happened to catch it, which is the failure this
protocol exists to prevent.

Two causes reach the pair - a plant-wide safe request, and the operator
switching one program off - and they park the same actuators the same way, so
the STATE is one value. Which one asked is `safe_requested()` /
`disable_requested()`. Where an actuator has to go is a property of the actuator
and not of the reason: a tray valve fails open, an inlet valve fails shut,
whichever asked. A program the operator had already switched off is therefore
ALREADY where a reboot needs it.

### Where the work goes

| hook | when | for |
|---|---|---|
| `run()` | every activation, inhibited or not | the control law, and driving to the fail position |
| `observe()` | every activation, in every state | status values, alarm conditions |

Neither runs on a `FAULTED` program.

An actuator moved from `observe()` would move while the program is inhibited,
which is the one thing that state promises does not happen.

### What gets said about it

`Plc::publish_health()` writes each program's verdict to the row it declared and
logs the CHANGES, walked by the supervisor rather than by each program - one that
is not being activated is exactly when a reader wants the row, and it cannot
write its own then.

| entering | level |
|---|---|
| `FAULTED` | critical |
| `DEGRADED` | warning |
| `HEALTHY`, `INHIBITED`, `PARKING` | notice |

Notice and not debug, because `LOG_MIN_LEVEL` is `LOG_INFO` on the OPTA: below it
the only record that a plant went to its safe state is one the hardware never
keeps. The whole set of them lands on one deliberate operator command.

`HEALTH_QUIET_MS` suppresses the log for the first seconds so bring-up does not
announce the first verdict of every program. `FAULTED` is exempt: setup is the
only place it is reached, so obeying the window would mean an unwireable program
was never reported at all.

`programs status` is the on-demand view - a tally per state, with the members of
`FAULTED`, `DEGRADED` and `PARKING` named. `HEALTHY` and `INHIBITED` are counted
and not listed: with control withheld or before the first scan every program is
inhibited, and a screen of names says less than the number does.

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
