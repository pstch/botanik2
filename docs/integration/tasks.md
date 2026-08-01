# Control programs {#int_tasks}

Control logic lives in the **scan cycle**: a table of tasks, each with a declared
rate, run by `Scan::Driver` once per beat between the input refresh and the output
write.

```cpp
struct Task {
    const char* name;
    uint32_t    period_ms;      // 0 = every beat
    void      (*run)(ScanContext&);
};

const Scan::Task g_tasks[] = {
    { "supply_watch",  100,  &supply_watch  },
    { "supply_report", 1000, &supply_report },
};
```

The table is const and flash-resident; a parallel `Scan::TaskStats[]` of the same
length holds the mutable scheduling state and must be **defined in a `.cpp`** (a
header definition breaks the link the moment two translation units include the
site).

## What a task gets

```cpp
struct ScanContext {
    uint64_t    now_ms;   // beat timestamp; identical for every task this beat
    uint32_t    dt_ms;    // ACTUAL ms since THIS task last ran
    uint16_t    missed;   // activations dropped since the last run; normally 0
    Vars::Bank& io;       // the variables to read and drive
};
```

Two guarantees the scaffolding provides, and that programs rely on:

**Coherency.** Inputs refresh once per beat, before any task runs, so every task in a
beat sees the same instant. There are no per-task snapshots: tasks run sequentially
in one thread and never span a beat.

**Lateness.** A task that comes due late runs **once** - missed activations are
dropped, not caught up, because a catch-up burst arrives exactly when the system is
already behind. `missed` carries the dropped count for logic that cares.

@warning `dt_ms` is therefore the **actual** elapsed time since that task last ran,
never the nominal period. A timer or integrator handed the nominal period after a
stall silently loses the time it was late by. Always pass `c.dt_ms`.

## Writing one

```cpp
inline void supply_watch(ScanContext& c) {
    static Scan::Ton bad_for;    // debounce
    static Scan::Sr  lockout;

    float ln = c.io.var(V_PWR_LN_V).value();
    bool  unhealthy = (ln < 200.0f) || (ln > 260.0f);

    // reset-dominant: recovery wins, so the lockout cannot stick
    lockout(bad_for(unhealthy, 5000, c.dt_ms), !unhealthy);

    c.io.var(V_SYS_SUPPLY_LOCKOUT).set(lockout.q());

    if (lockout.q()) {
        c.io.var(V_CLI_WINDOW1_OPN).set(false);
        c.io.var(V_CLI_WINDOW2_OPN).set(false);
    }
}
```

Note what it does **not** do: no clock reads, no direct I/O, no knowledge of Modbus.
It reads variables, applies blocks, and drives variables. Two idioms worth copying:

- **Let the default do the work.** A meter that has stopped answering reads as its
  declared `def` (0 V here), which is outside the healthy band - so the program
  handles a dead sensor correctly without ever testing `valid()`.
- **Publish the decision, not just the effect.** `V_SYS_SUPPLY_LOCKOUT` exists so the
  console, shell and telemetry can see *why* the windows are held. A coil reading 0
  does not distinguish "not commanded" from "locked out".

## The function-block library

`scan/blocks.h`. Ordinary RAM classes, declared `static` inside a task or as members.
Every timed block takes `dt_ms` explicitly rather than reading a clock - which is
what makes a late task correct, and a block testable without a clock at all.

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

### Analog

| Block | Call | Does |
|---|---|---|
| `Hysteresis` | `(value, low, high)` | true at or above `high`, false at or below `low`, holds in between - stops actuator chatter on a setpoint |
| `Ramp` | `(target, rate_per_s, dt_ms)` | rate limiter. The first call seeds to `target` without ramping; `preset()` jumps for a mode change |
| `Deadband` | `(value, band)` | reports `value` only once it has moved more than `band` from the last reported figure |

All expose `q()` or `value()` for the current state and `reset()` where it makes
sense.

## When tasks run

The scan is stepped once per beat, **only while control authority is enabled**. When
control is withdrawn the scan is inhibited entirely and outputs sit at their safe
state - see @ref user_control for the operator's view and @ref dev_architecture for
where in the loop this sits.
