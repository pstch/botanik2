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

### The one place that decision bites

`gh_mlb41` declares two programs, and only one of them has a group:

| program | declared | nearest group |
|---|---|---|
| `supply_watch` | 100 ms | `GROUP_SLOW` (100 ms) - exact |
| `supply_report` | 1000 ms | `GROUP_BACKGROUND` (500 ms) - **2x faster than declared** |

Nothing runs at 1000 ms. Three ways out, and this wants settling before step 5:

1. **Add a fifth group** (1000 ms) for reporting-rate work. Costs one enum entry
   and one config value, and every declared period keeps its meaning.
2. **Let `supply_report` run at 500 ms.** It reports rather than drives, so twice
   the rate is probably harmless - but it is a behaviour change, and the site
   asked for 1000.
3. **Retune `GROUP_BACKGROUND` to 1000 ms.** Rejected: it also carries
   `expire_stale`, whose 500 ms is argued for on its own terms.

Option 1 unless told otherwise: it is the only one where "the site declared a
rate and got it" stays true.
