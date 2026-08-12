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

## Verifying a registration

Learned the hard way while landing steps 1-3, and both cost a round trip.

**A missing registration is invisible.** A refused or skipped `add()` does not
fail the build and does not fail a test - the work simply never runs. Neither
`make` nor the 499-case suite noticed three components failing to register.

**Ask the shell, never the TASKS page.** `tasks <name>` answers "no task named"
or prints the detail, and that is decisive. The TASKS page is a NAME-SORTED
scrolling table, so a scroll that stops short reads exactly like an absent task -
which is what sent one correct commit to the bin before the shell said otherwise.

```
botanik> tasks ntp        # "no task named ntp", or its detail
```

**Build BOTH targets.** `stacks.h` naming `Tasks::Registry` in a method body with
only a forward declaration in scope compiled cleanly under the simulator's
include order and failed on the OPTA one. The sim build alone would have shipped
it.

**Still owed: a test that pins the expected task set.** It is the only thing that
would turn a silent non-registration into a failure, and every remaining step
adds more registrations to get wrong. Write it against `tasks <name>`, not the
page. One attempt is in the history and did not pass first time - the wait
predicate against the shell reply is the fiddly part.

## Migration order

1. ~~`Tick`, the registry on `{owner, handler}` entries, `add()` /
   `add_setup()` / `add_program()`.~~ **Done** (`d3eb36a`). The old path is still
   alive beside it.
2. ~~`LoopScheduler` takes the registry, the setup pass and the watchdog.~~
   **Done** (`ea31d73`). `kick(healthy)` returns false when it WITHHELD the kick,
   so the health verdict and the safe-state latch stay with the firmware.
3. ~~Components register themselves, innermost first.~~ **Done** (`142b159`,
   `8890074`): `AlarmController` out of `HealthMonitor`, and `NetworkHandler` /
   `NtpClient` / `MqttClient` out of `NetworkStack::step()`, which is now
   `register_tasks()`. Nothing steps anything.

   The shape that worked, and to reuse: `register_tasks()` **public** on the
   class (a public base is not enough - the override inherits the access of the
   section it is declared in), and the handler a **static MEMBER**, since the
   hook it calls is protected and a member reaches it without a friend.
   `MqttSender` kept the plain-function form: it is not a Component, and giving
   it a lifecycle purely to register it would be backwards.
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

#### Deleting `sys_supply_hz`

It is not an internal value: `vars.h:451` declares it `SV(..., reg 501)`, so
**register 501 leaves the published map**. SV names and scaling are an external
contract (MQTT telemetry and the Modbus TCP slave), so a master polling 501 stops
getting an answer. Registers are hand-assigned precisely so inserting a row never
renumbers the others - deleting one is the other direction, and wants to be a
deliberate note to whoever owns the SCADA side.

The **"Supply Hz" home field** (`vars.h:544`) points at `grid_Hz` instead. Left
alone it would resolve to the inert sink and `test_site_names.cpp` would fail the
gate, which is the mechanism working rather than a surprise.

## Whoever writes a row judges it

`Bank::reject_implausible()` goes, and with it its `GROUP_BACKGROUND` task in the
INPUT phase. It swept every row for values outside their declared band; that
belongs to whatever commits the value, which is the only thing that knows a write
just happened.

A row nobody writes needs no sweep either: it sits at `Q_NO_DATA`, which is 0 on
purpose so a zero-initialised `Value` reads as "nothing has written this" and
never as good data. Invalid-because-unwritten is already the default, not
something to detect.

`F_BOUNDED` keeps its meaning - it tells the writer whether the band is a real
one - and the band itself stays on the row, since the writer reads it there.

**`expire_stale()` stays.** Staleness is a different question from plausibility:
it is about a row that WAS written and stopped, which no writer can notice by
definition, because the thing that would have noticed is the write that did not
happen.

### Considered and rejected: expiring from the master instead

The symmetry is tempting - the master notices a device that stops answering, so
let it expire that device's rows and delete the sweep. It does not hold, because
a row goes stale in cases where there is no failure event to hang it on:

- **A starved block.** `_pick()` has an explicit "starved" tier precisely because
  a block can miss its period under bus contention. The device answers everything
  it is asked; these rows are simply not being asked for.
- **A row the planner never placed.** `rows_unscanned()` exists and is a bring-up
  critical, so this is a real state. Nothing ever fails for those rows because
  nothing ever asks.
- **The master not running at all** - compiled out, or its own task starved. That
  makes a safety property depend on the health of the thing it is watching.

There is also a granularity mismatch: `timeout_ms` is per ROW, a device failure
is per DEVICE, and a block spans rows that may declare different timeouts.

And it saves almost nothing. `expire_stale` is `GROUP_BACKGROUND`, so it is ~260
rows twice a second - about 500 row-visits per second, each a role check and an
integer compare. (The 25,900/s figure in the older scheduler notes predates the
move off `GROUP_FAST`.)

So: an independent check traded for a dependent one, to save something
unmeasurable. Keep the sweep.
