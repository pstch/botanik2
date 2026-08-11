# The scan cycle: one dispatcher, two registered kinds

A plan, not a description. Nothing here is built yet.

`LoopScheduler` becomes the thing that runs the scan cycle: it paces the beat, it
holds the registry, it runs every setup handler, and it owns the watchdog.
Everything the firmware does is a unit registered with it. No object steps
another.

## Why

Today a component is registered by the composition root, which picks its phase;
the component states the same phase again by which hook it overrides, and nothing
checks the two agree. A mismatch compiles, dispatches, and does nothing forever.
Meanwhile several objects step each other by hand - `HealthMonitor` steps
`AlarmController`, `NetworkStack` steps `NtpClient`/`MqttClient`, `Scan::Driver`
steps the programs - so an owner has to know its children's phase, and none of
those children gets a row of its own on the TASKS page.

Both problems come from the same place: the unit of scheduling is not the unit of
registration. Make the component register its own work, naming the phase once,
and both disappear.

## The two kinds

**Task** - a name, a rate group, a phase, and one handler. Registered by the
component that owns the work.

**Program** - a control program. Registered with a name and a group but **no
phase**: a program runs in the process phase or not at all. A program may also
register as the **provider** of an output value, and an output has at most one
provider - a second registration is refused and counted, so two programs driving
one row is a bring-up error rather than the latched `VF_TWO_WRITERS` flag it is
today.

**Component** is neither. It is the state the handlers act on, plus the
subsystem lifecycle (announce, config-serial watch, `healthy()`). It carries no
timer, no `due()`, no `dt_ms`, no per-beat verdict.

## Registration

```cpp
struct Tick { uint64_t now_ms; uint32_t dt_ms; uint16_t missed; };

class Component {
public:
    virtual void register_tasks(Tasks::Registry&) {}
};

// console.cpp - file-local, beside the methods they call
namespace {
    void con_setup(Component* c, const Tick& t) { static_cast<Console*>(c)->bring_up(t); }
    void con_in   (Component* c, const Tick& t) { static_cast<Console*>(c)->take_input(t); }
    void con_draw (Component* c, const Tick& t) { static_cast<Console*>(c)->draw(t); }
    void con_flush(Component* c, const Tick& t) { static_cast<Console*>(c)->flush(t); }
}

void Console::register_tasks(Tasks::Registry& r) {
    r.add_setup(*this, &con_setup, "console");
    r.add(*this, &con_in,    "console_in", GROUP_FAST, PHASE_INPUT);
    r.add(*this, &con_draw,  "console",    GROUP_SLOW, PHASE_PRESENT);
    r.add(*this, &con_flush, "console_tx", GROUP_FAST, PHASE_PRESENT);
}
```

The registry stores `{Component* owner, void (*run)(Component*, const Tick&),
name, group, phase, timer, cost}`.

`Tick` carries the elapsed time because the rate lives on the TASK, so the task
is the only thing that knows it. That is also what lets `Component` drop its
timer: `dt_ms` and `missed` reach a handler as arguments instead of as state.

### Why a named static and not a member pointer

`void (Console::*)(const Tick&)` does not convert to
`void (Component::*)(const Tick&)` - pointer-to-member converts base to derived,
not derived to base - so a member pointer cannot be type-erased into the table
without a template. The named static is the only form that lets the component
name its handler and the registry store it. It is file-local, greppable and
breakpoint-able, and the cast is confined to the one line that sits beside the
method it calls.

### Why not virtual hooks named after phases

They allow only ONE task per phase, and `Console` needs two in PRESENT at
different rates (draw at `GROUP_SLOW`, flush at `GROUP_FAST`). They also restate
the phase, which is the mismatch this rework exists to remove.

## What LoopScheduler owns

- the registry, and the phase-major dispatch
- every **setup handler**, run before the loop starts. Bring-up ordering becomes
  a property of the registry rather than of a 90-line `BotanikFirmware::setup()`
  with a comment explaining where `_tasks.setup()` has to sit.
- the **watchdog**. The kick means "this beat completed", so it belongs to
  whatever owns the beat, and withholding it when unhealthy belongs there too.
- the **program list**, the authority gate, and the two authority edges.

## Scan::Driver is gone

The registry owns the program list and decides which is due, so `scan/`
disappears as a directory. `Plc::Program` stays in `plc/`. Two behaviours move to
`LoopScheduler` with the list and **must not be lost**:

- **On withdrawal, `reset()` every program.** From that moment the plant moves
  without us, so what a program remembered is stale.
- **On return, reseed every timer.** Otherwise the first activation back reports
  the whole outage as its `dt`.

`SCAN_LATE` stops being special: it falls out of the task's own `missed` count,
the same mechanism every other task already has.

## BufferedSerial is not a component

It is a ring buffer and a port. It is owned by the component that uses it -
`Console`, `MonitorLogWriter` - and the pumping folds into that owner's own
tasks, because the pump and the thing that fills it are the same beat's work:

```cpp
void Console::take_input(const Tick&) { _serial.receive();  _pump_input(); }
void Console::flush(const Tick&)      { _term.present();    _serial.transmit(); }
```

`console_rx`, `console_tx`, `monitor_rx` and `monitor_tx` stop being separate
tasks. Nothing consumes the monitor port's received bytes - the drain only keeps
the ring from filling - so it folds into the writer's single task.

The RX-at-the-top / TX-at-the-bottom rule survives, but as two methods on one
class rather than four tasks the composition root has to place at opposite ends
of the beat.

## Invariants to carry across

- Inputs age out before anything reads, so one beat is coherent.
- Health runs before the logger drains, and before the process phase.
- The watchdog kick is withheld when unhealthy, outputs dropped to safe first.
- Setup runs after every log sink exists (it is what runs `announce()` and the
  HealthMonitor POST) and out of the first beat (the parameter load is tens of
  ms of flash).
- Order within a phase is registration order.

## Migration order

1. `Tick`, the registry on `{owner, handler}` entries, `add()` / `add_setup()` /
   `add_program()`. Old path kept alive so nothing else moves.
2. `LoopScheduler` takes the registry, the setup pass and the watchdog.
3. Components register themselves, innermost first (the ones currently stepped by
   another: `AlarmController`, `NtpClient`, `MqttClient`, `NetworkHandler`).
   Delete the hand-stepping as each lands.
4. `Console` / `MonitorLogWriter` absorb their serial; drop `BufferedSerial`'s
   Component base.
5. Programs register; `Scan::Driver` and `scan/` deleted; authority edges move.
6. `Component` loses the six phase hooks, the timer and the per-beat verdict.
7. Tests.

## Decided

- **The process phase IS `PHASE_CONTROL`.** No rename; "process phase" above is
  just prose for the phase that already exists.
- **Programs move onto the four rate groups.** A program registers a group like
  any other unit and loses its free `period_ms`.

### Both reference programs go away

`gh_mlb41` declares two programs, and neither survives the rework:

| program | was | becomes |
|---|---|---|
| `supply_watch` | 100 ms; debounce + lockout over the supply | nothing - the **Modbus master** sets quality itself |
| `supply_report` | 1000 ms; deadbands `grid_Hz` into `sys_supply_hz` | nothing - the derived row is deleted |

**Quality is the master's job, not a program's.** Each row already declares its
plausibility band (`min`/`max`, `F_BOUNDED`), and the master is the thing that
commits the reading - so it judges the value against the band as it writes it,
and a site does not write a program to watch for a bad reading.

Which settles the rate question by removing it: nothing is left asking for a
period the four groups cannot express, so **no fifth rate group is needed**.
`GROUP_BACKGROUND` stays at 500 ms and keeps its argument (`expire_stale`).

It also empties step 5 of the migration - the program registration path still
needs building, but no existing program exercises it.

#### Two loose ends in deleting `sys_supply_hz`

It is not an internal value. `vars.h:451` declares it `SV(..., reg 501)`, so:

- **Register 501 leaves the published map.** SV names and scaling are an
  external contract (MQTT telemetry and the Modbus TCP slave), so a master
  polling 501 stops getting an answer. Registers are hand-assigned precisely so
  inserting a row never renumbers the others - deleting one is the other
  direction, and wants to be a deliberate note to whoever owns the SCADA side.
- **The "Supply Hz" home field is orphaned** (`vars.h:544`). Point it at
  `grid_Hz`, or the name resolves to the inert sink and `test_site_names.cpp`
  fails the gate - which is the intended behaviour, not a surprise.

#### And one question it raises

If the master judges plausibility as it commits, what is left for
`Bank::reject_implausible()`, which sweeps every row at `GROUP_BACKGROUND` for
exactly that? Either it becomes the fallback for rows no master writes (local
I/O, computed values), or it goes. Decide when the master change lands.
