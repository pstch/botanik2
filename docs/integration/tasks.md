# Control programs {#int_tasks}

Control logic lives in the **scan cycle**: a table of programs, each with a
declared rate, run in PHASE_CONTROL between the input refresh and the output
write. There is no driver stepping them - **a program registers itself with the
task registry** like everything else in the beat, so it gets its own rate, its
own row on the TASKS page and its own cost sample.

A program is one class, deriving `Plc::Program`: its header declares what it
connects to and what it holds, its `.cpp` carries the body.

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

A program is one directory and two files: the header is its whole declaration -
its rows, the blocks it holds, and the `bind()` that resolves every name - and
the `.cpp` is the law.

```
programs/sense/strat_sensor/
    strat_sensor.h      the interface: rows, blocks, bind()
    strat_sensor.cpp    the bodies
```

```cpp
class StratSensor final : public Plc::Program {
public:
    /** @name VAR_INPUT
     *  @{ */
    Vars::RefIn<float> ceiling;
    Vars::RefIn<float> floor;
    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */
    Vars::RefOut<float> strat;
    /** @} */

private:
    Vars::VarRef _watch_rows[2];  ///< swept for validity before each activation

public:
    StratSensor(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "strat", Tasks::GROUP_FAST, io) {}
        //                     ^ the instance name, and the rate class

protected:
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
};

inline void StratSensor::bind(Vars::Bank& io) {
    ceiling = io.resolve_in<float>("cli_temp_hi");
    floor   = io.resolve_in<float>("cli_temp_lo");
    strat   = io.resolve_out<float>("cli_dTStrat_degC");

    _watch_rows[0] = ceiling.r;
    _watch_rows[1] = floor.r;
    set_inputs(_watch_rows, 2);
    set_health_row(io.resolve_out<uint8_t>("prog_strat"));
}
```

All the `.cpp` holds is the law, and it reads the rows as its own members:
`strat.set(Plc::abs_of(Plc::sub(ceiling.get(), floor.get())), measured)`.

**A name without a leading underscore is part of the interface**; the law's own
half - the state the table cannot describe, and its helpers - takes the
underscore that says so. Neither may collide with `Plc::Program`'s own
(`health`, `group`, `run`, ... and `_health`, `_slot`, ...).

The direction is the TYPE, and each promises something the compiler then holds
you to:

| Member | IEC 61131-3 | Rule |
|---|---|---|
| `RefIn<T>` | `VAR_INPUT` / `VAR_EXTERNAL CONSTANT` | read-only: `set()` on it does not compile |
| `RefOut<T>` | `VAR_OUTPUT` / `VAR_EXTERNAL` | this program is the row's ONE producer, claimed at bind time |
| `Ref<T>` | `VAR_IN_OUT` | read-modify-write, SHARED by declaration |

A member may be an ARRAY of references, resolved in a loop over numbered rows:
`RefIn<float> days[8]`, filled from `irr_days_tray1` .. `irr_days_tray8`. The
count is the array's own, so a row quietly added or removed is a row nothing
resolves - which is a bind failure, not an array that changed size.

A `resolve_*<T>` whose `T` disagrees with the row's own declared type resolves
to the inert sink, exactly as a missing name does: the row is bound by name AND
type.

@note **Declaring an output claims it**, inside the program's own writer window,
so two programs producing one row is a BRING-UP failure rather than something
noticed on whichever beat both first wrote - which for a row only a fault path
drives could be months in.

## Registering one

Nowhere, by hand. Programs are members of the site's firmware class, and the
member, its constructor init and its `register_tasks()` call are one list
expanded three times:

```cpp
#define X(cls, mem) Site::cls mem;
    SITE_PROGRAM_LIST(X)          // programs.h, in EXECUTION order
#undef X
```

**Registration order is execution order** within `PHASE_CONTROL`, so a law that
computes a setpoint and a law that acts on it complete in one beat when written
that way round. The order is `SITE_PROGRAM_LIST`'s, in `programs.h`, and that is
the one place to change it: the macro is what keeps the three lists from
drifting about where a program runs.

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

A worked one, in full. The header declares what it has:

```cpp
class SupplyWatch final : public Plc::Program {
public:
    Vars::RefIn<float> ln;
    Vars::RefOut<bool> locked;

private:
    Plc::Ton _bad_for;   ///< debounce
    Plc::Sr  _lockout;

public:
    SupplyWatch(const SetupContext& ctx, Vars::Bank& io)
        : Plc::Program(ctx, "supply_watch", Tasks::GROUP_SLOW, io) {}

protected:
    void bind(Vars::Bank& io) override {
        ln     = io.resolve_in<float>("grid_LN_V");
        locked = io.resolve_out<bool>("sys_supply_lockout");
    }
    void run(ProgramContext& c) override { (void)c; }
    void observe(ProgramContext& c) override;
};
```

and the `.cpp` is the law and nothing else:

```cpp
void SupplyWatch::observe(ProgramContext& c) {
    if (c.first_scan) { _bad_for.reset(); _lockout.reset(); }

    const bool unhealthy = !Plc::in_range(ln.get(), 200.0f, 260.0f);

    // reset-dominant: recovery wins, so the lockout cannot stick
    locked.set(_lockout(_bad_for(unhealthy, 5000, c.dt_ms), !unhealthy), true);
}
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
  error; report `degraded()` and let each actuator substitute its OWN safe
  state. A row passed to `set_inputs()` is swept for you and needs no call.
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
