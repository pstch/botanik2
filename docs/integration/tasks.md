# Control programs {#int_tasks}

Control logic lives in the **scan cycle**: a table of programs, each with a
declared rate, run by `Scan::Driver` once per beat between the input refresh and
the output write.

A program is a `Plc::Program` subclass. It holds its function blocks and its
resolved variable references as members, and overrides one method:

```cpp
class Program {
public:
    Program(const char* name, uint32_t period_ms);   // 0 = every beat

    virtual void bind(Vars::Bank& io) {}    // bring-up: names -> VarRefs, once
    virtual void run(Context& c) = 0;       // one activation
    virtual void reset() {}                 // control withdrawn: drop stale state
    virtual void resume() {}                // control returned: re-datum
};
```

`reset()` and `resume()` are the two control-authority edges, and they are not
the same event. On withdrawal the process moves without us, so what a program
REMEMBERED is stale - a filter's history, a position estimate. On return the
timers are reseeded so the first activation reports its nominal period rather
than the whole outage. A totaliser keeps its total across both; that is the
distinction the pair exists to express.

The site's table is an array of **pointers**, so it stays const and
flash-resident even though the programs it names live in RAM:

```cpp
Plc::Program* const g_programs[] = {
    &Programs::supply_watch,
    &Programs::supply_report,
};
```

There is no parallel array to keep in step with it: each program carries its own
scheduling state.

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
    SupplyWatch() : Program("supply_watch", 100) {}

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

    void reset() override { _bad_for.reset(); _lockout.reset(); }
};
```

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

`scan/` is not in that table on purpose. It is the *cycle* - which program is due,
how late, and running it - not the vocabulary programs are written in.

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
that uses them. Every timed block takes `dt_ms` explicitly rather than reading a
clock - which is what makes a late program correct, and a block testable without a
clock at all.

| Header | Blocks |
|---|---|
| `timers.h` | `Ton` `Tof` `Tp` |
| `logic.h` | `RTrig` `FTrig` `Sr` `Rs` `Interlock` |
| `analog.h` | `Hysteresis` `Hysteresis3` `Ramp` `Deadband` |
| `filters.h` | `Ewma` `MedianOf3` `MovingAverage<N>` |
| `meter.h` | `Integral` `Derivative` |
| `pid.h` | `Pid` (+ `PidTuning`) |
| `actuator.h` | `Actuator` (+ `ActuatorConfig`) |

### Timers

| Block | Call | Does |
|---|---|---|
| `Ton` | `(in, delay_ms, dt_ms)` | on-delay: true once `in` has held true for `delay_ms`; false immediately on drop |
| `Tof` | `(in, delay_ms, dt_ms)` | off-delay: follows `in` up, holds true `delay_ms` after it drops |
| `Tp` | `(in, width_ms, dt_ms)` | pulse: a rising edge starts a fixed pulse that runs to completion regardless of `in` |

### Edges and latches

| Block | Call | Does |
|---|---|---|
| `RTrig` | `(in)` | true for the one scan where `in` went false to true |
| `FTrig` | `(in)` | falling edge; assumes the input starts low, so neither trigger fires on the first scan |
| `Sr` | `(set, reset)` | **set**-dominant latch |
| `Rs` | `(set, reset)` | **reset**-dominant latch |

`Rs` is the safer default for anything an interlock can stop: it means recovery
wins.

### Interlock

| Block | Call | Does |
|---|---|---|
| `Interlock` | `(in0, in1, in2, in3, reset)` | four permissives in, one permit out; latches on a trip and records which input went first |

Every input is a *permissive*: true means the condition is satisfied. Two things
it does that an `and` of four booleans does not:

- **it latches.** A condition that trips and clears itself must still be seen and
  acknowledged. The acknowledge is an edge, so a reset wired true cannot stand as
  blanket permission. A latching interlock also starts *without* the permit -
  after a power cycle nothing arms itself, the same reasoning as control authority
  starting disabled. `Interlock il(false)` opts out and just follows the inputs.
- **it remembers which one went first.** When four things can stop a machine, the
  rest usually fall over as a consequence of the first. `first_out()` gives the
  index and `first_out_name()` the label, held until the acknowledge, so the log
  line can say *why* rather than "interlocked".

```cpp
static Scan::Interlock permit("door", "supply", "estop", "vent");
if (!permit(door_shut, supply_ok, !estop, vent_ok, ack_pressed)) {
    c.io.var("sys_trip_cause").set(permit.first_out_name());
}
```

A site with three conditions passes `true` for the fourth; one with six chains two
interlocks.

### Analog

| Block | Call | Does |
|---|---|---|
| `Hysteresis` | `(value, low, high)` | true at or above `high`, false at or below `low`, holds in between - stops actuator chatter on a setpoint |
| `Hysteresis3` | `(value, low, high, hyst)` | three bands - `BAND_LOW` / `BAND_MID` / `BAND_HIGH` - each edge with its own `hyst` hold |
| `Ramp` | `(target, rate_per_s, dt_ms)` | rate limiter. The first call seeds to `target` without ramping; `preset()` jumps for a mode change |
| `Deadband` | `(value, band)` | reports `value` only once it has moved more than `band` from the last reported figure |

`Hysteresis3` is the shape a comfort band actually needs: one `Hysteresis` answers
"is it too warm", a greenhouse asks "too cold, right, or too warm" and wants a
neutral middle where nothing runs. It seeds from the raw thresholds on its first
call, so a block starting mid-band reports that band instead of waiting for an edge
it may never see.

### Filters

| Block | Call | Does |
|---|---|---|
| `Ewma` | `(x, tau_s, dt_ms)` | first-order lag with a time constant in seconds; adopts the first sample rather than creeping up from zero |
| `MedianOf3` | `(x)` | median of the last three samples - kills a single garbage conversion outright, which an average only dilutes |
| `MovingAverage<N>` | `(x)` | boxcar mean of the last N samples; forgets a bad one completely after N of them |

`Ewma` derives its per-scan blend from `tau_s` and the actual `dt`, so a 30 s
filter is a 30 s filter whether its task runs at 1 Hz or 10 Hz. `MovingAverage` is
sample-counted, not time-counted - prefer `Ewma` when wall-clock response is what
matters. (These are not `utils/filters.h`, which is the scheduler's own per-beat
metric instrumentation.)

### Metering

| Block | Call | Does |
|---|---|---|
| `Integral` | `(rate_per_s, dt_ms, cutoff)` | integrates a rate into a running quantity: volume from flow, energy from power, hours from a run bit |
| `Derivative` | `(quantity, window_ms, dt_ms)` | differentiates a quantity back into a per-second rate: flow from a pulse count, fill rate from a level |

The IEC 61131-3 names; instrumentation calls the first a *totalizer* and the second
a *rate block*. Inverses of each other, and a plant usually has both. Every rate
here is **per second**, converted at the call site where the units are visible: a
meter reading L/min goes in as `flow / 60.0f` and comes back out as L/min by
`* 60.0f`.

Two details that are not incidental. `Integral` accumulates in `double` - a
`float` total loses its low bits as it grows, and somewhere around 10000 L an
increment of a few millilitres stops changing the value at all, so the total
silently stops advancing; `total()` still hands back a `float` for the variable
table. Its `cutoff` is the standard low-flow cut: a meter idling on a few counts of
noise would otherwise total a plausible-looking volume overnight with the pump off.
`Derivative` measures over a **window** rather than scan to scan, because a
quantity that advances in steps (one pulse, one litre) differentiated every scan
gives a rate alternating between zero and a spike.

### PID

| Block | Call | Does |
|---|---|---|
| `Pid` | `(sp, pv, tuning, dt_ms)` | holds a reading at a setpoint by driving a continuous output |

`PidTuning` is separate from the block because it is *data*: build it each scan
from parameters and a loop is retunable from the PARAMS page without a build, while
the block keeps the loop's state across the change. Gains are the parallel form, so
each is in engineering units - `kp` output per unit of error, `ki` per unit-second,
`kd` per unit-per-second.

```cpp
static Scan::Pid heat;
const Scan::PidTuning t(Vars::get_f32(c.io, "heat_kp"),
                        Vars::get_f32(c.io, "heat_ki"), 0.0f, 0.0f, 100.0f);
c.io.var("heat_demand").set(heat(setpoint, air_temp, t, c.dt_ms));
```

Three departures from the textbook sum, each of which is the difference between a
loop that works on a greenhouse and one that works on paper:

- **derivative on the measurement, not the error** - a setpoint typed at the
  console is a step, and differentiating it slams the output to a limit for one
  scan. Optionally lowpassed (`d_tau_s`), since differentiating a noisy sensor
  amplifies exactly the noise.
- **conditional integration** - while the output sits at a limit the integral is
  frozen if it would push further into it, so a loop that spends the night against
  its ceiling comes off it the moment the error changes sign rather than unwinding
  for an hour first.
- **the integral clamped to the output span** - a term larger than any output the
  actuator can produce is a value the loop must spend real time undoing.

Direct-acting by default (output rises as the reading falls: heating, filling). A
cooling or venting loop sets `reverse` in the tuning, which flips the error and the
derivative together - swapping the two arguments instead would silently
differentiate the setpoint. `preset()` hands a manual output back to the loop.

### Two-position actuator

| Block | Call | Does |
|---|---|---|
| `Actuator` | `.step(target, cfg, dt_ms)` | drives an open/close coil pair to a fractional position by timing the strokes |

A window drive, a shade motor, a motorised valve: the field gives you two
contactors and nothing back. Position is therefore an **estimate**, integrated from
how long each coil was energised against the configured travel times (`open_ms` and
`close_ms` separately - a window that opens against its own weight closes faster,
and one figure for both walks the estimate off by a few percent per stroke).

```cpp
static Scan::Actuator window;
static const Scan::ActuatorConfig cfg(45000, 38000);   // travel times
window.step(demand, cfg, c.dt_ms);
c.io.var("win_open_cmd").set(window.open_cmd());
c.io.var("win_close_cmd").set(window.close_cmd());
c.io.var("win_position").set(window.position());
c.io.var("win_state").set(window.state_name());
```

Five states: `ST_UNKNOWN`, `ST_CLOSING`, `ST_CLOSED`, `ST_OPENING`, `ST_OPEN`.
`CLOSED` means the block believes the actuator is **on its seat**, which only a
stroke to the closed end produces; anything else at rest is `OPEN`, with
`position()` saying how far. That is the valve convention, and the useful one -
"closed" is a claim about the seal, not about a number being small.

What keeps the estimate honest:

- a stroke to either end runs the estimated travel **plus `seat_ms`**, so every
  full stroke re-datums against the mechanical stop instead of against the
  estimate, and such a stroke is committed - nothing cuts it short at the estimated
  arrival, because that overrun *is* the datum;
- the estimate is not trusted before the first such stroke. That is what `UNKNOWN`
  is for: after a power cycle the actuator is wherever it was left, so the first
  command drives fully to an end before anything else - to the closed end, unless
  the target *is* the open end, in which case that stroke datums just as well.

The two outputs are never both true, a reversal always passes through `reverse_ms`
with both dropped, and a move shorter than `min_run_ms` or inside `deadband` is not
made at all - a twitch too short to overcome stiction wears the gear without moving
anything. `home()` forgets the position (a manual crank, hand/off/auto coming back
to auto); `stop()` drops the coils and keeps the estimate.

Control authority is not the block's business: a task that must stop driving calls
`stop()`, and the coil rows it publishes drop to safe on their own when control is
withdrawn.

All blocks expose `q()` or `value()` for the current state and `reset()` where it
makes sense.

## When programs run

The scan is stepped once per beat, **only while control authority is enabled**. When
control is withdrawn the scan is inhibited entirely and outputs sit at their safe
state - see @ref user_control for the operator's view and @ref dev_architecture for
where in the loop this sits.

Both edges of that gate reach the programs:

- **Withdrawn.** The driver calls `reset()` on every program, once, on the edge.
  From that moment the process moves without us - a vent creeps shut, a tank
  drains, a room cools - so anything a program remembered about the field is
  stale. Override `reset()` to drop what has gone stale; it is deliberately not
  "clear everything", because a dwell timer is stale but a totalizer holding the
  litres delivered today is a *record*, and zeroing it would be data loss.
- **Returned.** Every program's timer is reseeded, so its first activation back
  reports its nominal period rather than the whole outage. A `Ton` holding 5 s
  handed an hour of `dt` fires the instant control returns, which is the one thing
  a withdrawal exists to prevent. `dt` alone cannot express "the process moved
  while you were not looking" - that is what `reset()` is for.
