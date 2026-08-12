# Control programs {#int_tasks}

Control logic lives in the **scan cycle**: a table of programs, each with a
declared rate, run in PHASE_CONTROL between the input refresh and the output
write. There is no driver stepping them - **a program registers itself with the
task registry** like everything else in the beat, so it gets its own rate, its
own row on the TASKS page and its own cost sample.

A program is a `Plc::Program` subclass. It holds its function blocks and its
resolved variable references as members, and overrides one method:

```cpp
class Program {
public:
    Program(const char* name, Tasks::GroupId group);  // a RATE GROUP, not a period

    virtual void bind(Vars::Bank& io) {}    // bring-up: names -> VarRefs, once
    virtual void run(Context& c) = 0;       // one activation
};
```

Two hooks, and no control-authority hooks at all. While control is withdrawn a
program is simply **not called**, and nothing outside it reads what it remembers,
so there is nothing to do on the way down. On the way back up `c.first_scan` is
raised, and that is where a program drops what has gone stale - see *When
programs run* below.

The site's table is an array of **pointers**, so it stays const and
flash-resident even though the programs it names live in RAM:

```cpp
Plc::Program* const g_programs[] = {
    &Programs::supply_watch,
    &Programs::supply_report,
};
```

There is no parallel array to keep in step with it. **Table order is execution
order**: the firmware registers them in table order and the registry dispatches a
phase in registration order, so a law that computes a setpoint and a law that acts
on it complete in one beat when written that way round.

## What a program gets

```cpp
struct Context {
    uint64_t    now_ms;   // beat timestamp; identical for every program this beat
    uint32_t    dt_ms;    // ACTUAL ms since THIS program last ran
    uint16_t    missed;   // activations dropped since the last run; normally 0
    Vars::Bank& io;       // the variables to read and drive
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

```cpp
class SupplyWatch : public Plc::Program {
    Plc::Ton     _bad_for;    // debounce
    Plc::Sr      _lockout;
    Vars::VarRef _ln = { Vars::VAR_NONE }, _locked = { Vars::VAR_NONE };

public:
    SupplyWatch() : Program("supply_watch", Tasks::GROUP_SLOW) {}   // 100 ms

    void bind(Vars::Bank& io) override {
        _ln     = io.find("grid_LN_V");
        _locked = io.find("sys_supply_lockout");
    }

    void run(Plc::Context& c) override {
        const bool unhealthy = !Plc::in_range(c.io.var(_ln).value(), 200.0f, 260.0f);

        // reset-dominant: recovery wins, so the lockout cannot stick
        _lockout(_bad_for(unhealthy, 5000, c.dt_ms), !unhealthy);

        c.io.var(_locked).set(_lockout.q());
    }

};
```

The rate is one of the four groups (@ref dev_tasks), not a number of its own: a
program is scheduled like everything else in the beat, so retuning a rate class
retunes every program on it. Pick the group whose period the process time constant
can live with.

Note what it does **not** do: no clock reads, no direct I/O, no knowledge of Modbus.
It reads variables, applies blocks, and drives variables. Three idioms worth copying:

- **Let the default do the work.** A meter that has stopped answering reads as its
  declared `def` (0 V here), which is outside the healthy band - so the program
  handles a dead sensor correctly without ever testing `valid()`.
- **Resolve names in `bind()`, not in `run()`.** `Bank::find()` is a linear walk of
  the whole variable table. Done once at bring-up it costs nothing; done per
  activation it is the same search repeated forever to learn what the program
  already knew.
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
| PROGRAM | the site's `tasks.h` | blocks, and the field |

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
| `logic.h` | `RTrig` `FTrig` `Sr` `Rs` `Interlock` |
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
void run(Plc::Context& c) override {
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
