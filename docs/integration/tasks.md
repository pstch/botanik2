# Control programs {#int_tasks}

Control logic lives in the **scan cycle**: a table of programs, each with a
declared rate, run in PHASE_CONTROL between the input refresh and the output
write. There is no driver stepping them - **a program registers itself with the
task registry** like everything else in the beat, so it gets its own rate, its
own row on the TASKS page and its own cost sample.

A program is TWO classes: a generated base carrying its whole declaration part -
the rows it connects to and the function blocks it holds - and a hand-written
derived class carrying the body. The base derives `Plc::Program`:

```cpp
class Program {
public:
    Program(const SetupContext& ctx, const char* name, Tasks::GroupId group,
            Vars::Bank& io);              // a RATE GROUP, not a period

    virtual void bind(Vars::Bank& io) {}     // bring-up: names -> refs, once
    virtual void run(ProgramContext& c) = 0; // one activation
    virtual void observe(ProgramContext& c) {}   // report, in EVERY state
};
```

`run()` is the control law. `observe()` is what the program SAYS - status values
and alarm conditions - and it runs in every state, including while the plant is
parked, which is exactly when someone is looking. A watch program has an empty
`run()` and does all its work there.

While control is withdrawn a program is **not called**, and nothing outside it
reads what it remembers, so there is nothing to do on the way down. On the way
back up `c.first_scan` is raised, and that is where a program drops what has gone
stale - see *When programs run* below.

## Declaring one

A program's variables are not spelled in C++. Its own `<class>.toml` connects its
interface to the table, and `scripts/vars_gen.py` emits its base class - one
typed member per row, the blocks it declares, and the `bind()` that resolves
them all - into `programs/<stage>/<class>/<class>.gen.h`:

```toml
# programs/sense/strat_sensor/strat_sensor.toml
name   = "strat"          # the instance, and its writer identity
class  = "StratSensor"    # and the directory it lives in, in snake_case
task   = "FAST"           # the rate class it is dispatched at
health = "prog_strat"     # optional: where its health is published
desc   = "The stratification gap, unsigned."   # its @brief, and the report's

[in]
ceiling = "cli_temp_hi"
floor   = "cli_temp_lo"

[out]
strat = "cli_dTStrat_degC"
```

The directory holds three files, and the generator owns two of them - it
scaffolds the third once and never touches it again:

```
programs/sense/strat_sensor/
    strat_sensor.toml     declared: this file
    strat_sensor.gen.h    GENERATED: class Site::StratSensor : Plc::Program
    strat_sensor.cpp      yours: the bodies
```

All you write is the law, and it reads the rows as its own members:
`strat.set(ceiling.get() - floor.get())`. The instance name and the rate class
are baked into the generated constructor, so neither can drift.

**A name without a leading underscore was declared as an interface**; `[var]` and
`[fn]` are the law's own half, and the generator adds the underscore that says
so - `[var.seeded]` becomes `_seeded`. It refuses a name that would collide
either way: with `Plc::Program`'s interface (`health`, `group`, `run`, ...) or,
once underscored, with its internals (`_health`, `_slot`, ...).

Everything a program declares beyond its connections gets a section of its own,
because a type, an initial value and a sentence of prose do not fit on one line:

```toml
[var.seeded]
type = "bool"
init = "false"

[fn.phase_at]
type = "DayPhase (int16_t from_rise, int16_t from_set) const"
doc  = """
The phase two offsets land in, with the two BANK phases skipped when yesterday
did not arm them.
"""
```

Three sections, and each promises something the compiler then holds you to:

| Section | IEC 61131-3 | Rule |
|---|---|---|
| `in` | `VAR_INPUT` / `VAR_EXTERNAL CONSTANT` | read-only: `drive()` on it does not compile |
| `out` | `VAR_OUTPUT` / `VAR_EXTERNAL` | this program is the row's ONE producer, claimed at bind time |
| `inout` | `VAR_IN_OUT` | read-modify-write, SHARED by declaration |

A member is `name = "row"`, a list, or a PATTERN where `*` is one or more
digits: `days = { rows = "irr_days_tray*", count = 8 }` takes the eight numbered
trays in numeric order. `*` matches digits and nothing else, so it cannot take
`irr_traysPerDay` for a tray, and `count` is asserted so a row quietly added or
removed is an error rather than an array that changed size.
The reference's TYPE comes from the row, so a member cannot disagree with what it
names.

@note **Declaring an output claims it**, inside the program's own writer window,
so two programs producing one row is a BRING-UP failure rather than something
noticed on whichever beat both first wrote - which for a row only a fault path
drives could be months in. `check-vars` catches it earlier still, before the
firmware is built.

## Registering one

Nowhere, by hand. Programs are members of the site's firmware class, and the
member, its constructor init and its `register_tasks()` call are one generated
list expanded three times:

```cpp
#define X(cls, mem) Site::cls mem;
    SITE_PROGRAM_LIST(X)          // generated/programs.gen.h, in TABLE order
#undef X
```

**Registration order is execution order** within `PHASE_CONTROL`, so a law that
computes a setpoint and a law that acts on it complete in one beat when written
that way round. The order is `programs/order.toml`'s, and that is the file to change
to change it. The three lists used to be hand-kept and had already drifted about
where one program ran.

## What a program gets

```cpp
struct ProgramContext {
    uint64_t    now_ms;     // beat timestamp; identical for every program this beat
    uint32_t    dt_ms;      // ACTUAL ms since THIS program last ran
    uint16_t    missed;     // activations dropped since the last run; normally 0
    bool        first_scan; // first activation since control returned
    Vars::Bank& bank;       // the variables to read and drive
};
```

Two guarantees the scaffolding provides, and that programs rely on:

**Coherency.** Inputs refresh once per beat, before any program runs, so every
program in a beat sees the same instant. There are no per-program snapshots:
programs run sequentially in one thread and never span a beat.

**Lateness.** A program that comes due late runs **once** - missed activations are
dropped, not caught up, because a catch-up burst arrives exactly when the system is
already behind. `missed` carries the dropped count for logic that cares.

@warning `dt_ms` is therefore the **actual** elapsed time since that program last
ran, never the nominal period. A timer or integrator handed the nominal period after
a stall silently loses the time it was late by. Always pass `c.dt_ms`.

## Writing one

```toml
# programs/sense/supply_watch/supply_watch.toml
name  = "supply_watch"
class = "SupplyWatch"
task  = "SLOW"
stage = "sense"
desc  = "The supply rails, watched."
[block.bad_for]
type = "Ton"
doc  = "debounce"

[block.lockout]
type = "Sr"
[in]
ln = "grid_LN_V"
[out]
locked = "sys_supply_lockout"
```

The rows and the two blocks are declared, so the hand-written half is the law
and nothing else:

```cpp
class SupplyWatch final : public Gen::SupplyWatch {
public:
    SupplyWatch(const SetupContext& ctx, Vars::Bank& io)
        : Gen::SupplyWatch(ctx, io) {}          // SLOW: 100 ms, from the table

protected:
    void run(ProgramContext& c) override { (void)c; }

    void observe(ProgramContext& c) override {
        if (c.first_scan) { bad_for.reset(); lockout.reset(); }

        const bool unhealthy = !Plc::in_range(ln.get(), 200.0f, 260.0f);

        // reset-dominant: recovery wins, so the lockout cannot stick
        locked.set(lockout(bad_for(unhealthy, 5000, c.dt_ms), !unhealthy));
    }
};
```

It commands nothing, so all of it is in `observe()` and `run()` is empty: the
verdict has to keep being made while the plant is parked, and the actuator
controllers read it as an interlock rather than this program driving their coils.

The rate is one of the four groups (@ref dev_tasks), not a number of its own: a
program is scheduled like everything else in the beat, so retuning a rate class
retunes every program on it. Pick the group whose period the process time constant
can live with.

Note what it does **not** do: no clock reads, no direct I/O, no knowledge of Modbus.
It reads variables, applies blocks, and drives variables. Three idioms worth copying:

- **Let the default do the work.** A meter that has stopped answering reads as its
  declared `def` (0 V here), which is outside the healthy band - so the program
  handles a dead sensor correctly without ever testing `valid()`.
- **Reset a block on `first_scan`.** A timer or a latch is a claim about the
  recent past, and not being called means the program stopped watching. Resuming
  a dwell that was counting an outage nobody saw is worse than starting again.
- **Say what you cannot see.** A reading whose row is invalid is not a small
  error; report `degraded()`, or `faulted()` if the output goes with it, and let
  each actuator substitute its OWN safe state.
- **Publish the decision, not just the effect.** `sys_supply_lockout` exists so the
  console, shell and telemetry can see *why* the windows are held. A coil reading 0
  does not distinguish "not commanded" from "locked out".

## The POU library

A program is written out of two kinds of piece, and `plc/` keeps them apart the
way IEC 61131-3 does:

| POU | Here | Has |
|---|---|---|
| FUNCTION | `plc/functions/` | nothing. Same inputs, same output, always |
| FUNCTION_BLOCK | `plc/blocks/` | instance state, carried between scans |
| PROGRAM | the site's `programs/` | blocks, and the field |

The line between the first two is mechanical rather than a judgement call: **a
function is a single expression in a header, and a block needs a `.cpp`.** If it
has to remember something it is a block; if it does not, it is a function, and it
is `constexpr` so the compiler folds it into the caller.

There is no fourth entry for the *cycle* - which program is due, how late, and
running it. That is `Tasks::Registry`, the same table the whole beat is dispatched
from, and it is not part of the vocabulary programs are written in.

### PLC functions

`plc/functions/`, header-only.

| Header | Functions |
|---|---|
| `math.h` | `limit` `in_range` `deadzone` `scale` `lerp` `sel` |
| `time.h` | `seconds` `accumulate` `countdown` |

`limit(v, lo, hi)` is IEC's LIMIT with `std::clamp`'s argument order.
`in_range(ln, 200, 260)` is the plausibility test a program otherwise writes as a
pair of comparisons whose bounds have to be read twice to be trusted.
`deadzone(v, band)` is zero inside the band and *continuous* outside it - it
reports the excursion, so acting on it is not a step; it is the stateless cousin
of the `Deadband` block, which suppresses reports of small moves rather than
ignoring a small error. `scale()` reads as the low end when handed a degenerate
input span, because a misconfigured span must give a dull answer rather than an
infinity that propagates into an output.

`accumulate` / `countdown` take a reference to the caller's counter (IEC's
VAR_IN_OUT), which is why they are functions despite writing something: the state
belongs to the block that owns it, not to them.

### PLC function blocks

`plc/blocks/`, one header per family and `blocks.h` pulling in the lot (which
`libbotanik.h` already does). Ordinary RAM classes, held as members of the program
that uses them.

| Header | Blocks |
|---|---|
| `timers.h` | `Ton` `Tof` `Tp` |
| `logic.h` | `RTrig` `FTrig` `Sr` `Rs` |
| `analog.h` | `Hysteresis` `Hysteresis3` `Ramp` `Deadband` |
| `filters.h` | `Ewma` `MedianOf3` `MovingAverage<N>` |
| `meter.h` | `Integral` `Derivative` |
| `pid.h` | `Pid` (+ `PidTuning`) |
| `actuator.h` | `Actuator` (+ `ActuatorConfig`) |

Each block's own header is its reference: the call signature, the edge cases and
the reasoning behind them are Doxygen comments on the class, so they cannot drift
from the code. Three habits carry most of the correctness:

- **Seeding.** A filter or a rate limiter adopts its first sample rather than
  creeping up from zero - an `Ewma` warming up from 0 would report a frost that
  is not there. `preset()` re-seeds one for a mode change.
- **`dt_ms`, always.** No block reads a clock, so a program that ran late stays
  correct and every block is testable without one.
- **Latches state their dominance.** `Sr` is set-dominant and `Rs` is
  reset-dominant; picking the wrong one is the classic interlock bug.

## When programs run

The scan is stepped once per beat, **only while control authority is enabled**. When
control is withdrawn the scan is inhibited entirely and outputs sit at their safe
state - see @ref user_control for the operator's view and @ref dev_architecture for
where in the loop this sits.

That gate is the coarse one, and it is the only thing that stops an activation.
A `safe_request()` or an operator switching one program off is finer: the program
is **still activated**, and both hooks still run. `run()` reads
`inhibit_requested()` and drives to its fail position instead of to a demand,
calling `parking()` while it is still travelling there; `observe()` carries on
reporting, because a plant going into its safe state is exactly when someone is
watching a status row.

Only the RETURN edge reaches the programs, and it does two things:

- **The timer is reseeded**, so the first activation back reports its nominal
  period rather than the whole outage. A `Ton` holding 5 s handed an hour of `dt`
  fires the instant control returns, which is the one thing a withdrawal exists
  to prevent. (It also means a program does *not* run on the beat authority came
  back - it waits its full period.)
- **`c.first_scan` is raised on that activation.** `dt` alone cannot express "the
  process moved while you were not looking": a vent creeps shut, a tank drains, a
  room cools, so anything the program remembered about the field is stale. Drop
  it under the flag:

```cpp
void observe(ProgramContext& c) override {
    if (c.first_scan) { _bad_for.reset(); _lockout.reset(); }
    ...
}
```

  Deliberately opt-in rather than "clear everything": a dwell timer is stale, but
  a totalizer holding the litres delivered today is a *record*, and zeroing it
  would be data loss.

Nothing happens on the withdrawal edge, because nothing needs to. Clearing at the
return is not merely equivalent, it is better placed: the program is looking at a
freshly refreshed image, where a hook on the edge would see the one beat the edge
happened on and never the image its own next `run()` gets.

Bring-up needs no special case either. A task's first activation ever is a first
scan too - a program knows no more about the plant at bring-up than after an
outage.
