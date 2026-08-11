# Architecture {#dev_architecture}

Three rules shape everything else:

1. **Nothing blocks.** Every stateful object exposes a `step()` that does a small,
   bounded amount of work and returns. Blocking stalls the RTOS and trips the
   hardware watchdog.
2. **The platform is reached only through HAL.** No `<time.h>`, no Mbed, no board
   APIs outside `hal/`. This is what makes the simulator possible.
3. **A site is data.** The library never knows which greenhouse it is running.

## Layers

| Layer | Holds | Written by |
|---|---|---|
| `libbotanik` | every generic component, plus the `BotanikFirmware` base that owns them and drives the loop | shipped, reused as-is |
| site | one installation: its variable table, alarms, control programs, and a thin binding class | per installation |
| entry point | selects one site and runs it | per target |

Components are chained by inheritance: `BotanikFirmware` constructs everything and
exposes an accessor per component; the site derives it and hands over a
`SiteDescriptor`. See @ref integration for the site side.

## Subsystems

Under `src/libbotanik/`:

| Path | Holds | More |
|---|---|---|
| `hal/` | the platform interface: clock, memory ordering, stack, watchdog, serial, storage, network transport. `simulator.h` and `opta.h` implement it | @ref dev_porting |
| `core/` | the BOTTOM of the stack, and only that: `Component` and `SystemContext` | this page |
| `firmware/` | the composition root: `BotanikFirmware`, and the feature-gated stacks in `stacks.h` | this page |
| `scheduler/` | `LoopScheduler`, the fixed-period driver and its self-instrumentation, plus the `Tasks::Registry` the beat is dispatched from and its per-task cost sampling | @ref dev_scheduler |
| `vars/` | the variable model: descriptors, value slots, `Bank`, parameters | @ref dev_vars |
| `scan/` | `Scan::Driver`: which control program is due, and running it | @ref int_tasks |
| `plc/` | the POU library the programs are written in: `functions/` (stateless) and `blocks/` (stateful) | @ref int_tasks |
| `modbus/` | the field bus and the plant network: shared vocabulary in `wire.h`/`response.h`, `rtu/` and `tcp/` for the framing and all four corners | @ref dev_modbus |
| `alarms/` | `AlarmController`, one condition per site-declared alarm, plus the platform set | @ref dev_health |
| `health/` | `HealthMonitor`: POST, the platform alarms, the watchdog verdict | @ref dev_health |
| `logs/` | `Logger`, its rate limiter, and the `LogWriter` sinks | @ref dev_logging |
| `console/` | the VT device, the screen back-buffer, input parsing, the line editor | @ref dev_screen |
| `pages/` | the full-screen views behind one `Page` interface | @ref dev_pages |
| `shell/` | `CommandShell`: submission, built-ins, history, kill-ring | @ref user_shell |
| `network/` | the optional network / NTP / MQTT clients | @ref dev_network |
| `sun/` | solar-position tracking, standalone | - |
| `utils/` | `BufferedSerial`, metric filters, time and text helpers, the assert | - |

`TARGET_OPTA` appears only under `hal/`. Shared code never branches on it;
`TARGET_SIMULATOR` may, for development-only scaffolding.

## Bring-up

`setup()` is non-virtual and runs in this order:

@image html bringup.drawio.svg "Bring-up, in order. The two highlighted steps are the point of it: every sink exists before anything announces itself, and the field is not ours until the last line."

The last two are the point of the ordering: control authority starts **disabled**,
so nothing part-way through bring-up - a half-wired component, a faulting site hook -
can energise an actuator on the way past.

`tasks.setup()` sits where it does for two reasons. It is what runs each component's
`announce()` and the HealthMonitor's POST, and before the sinks above were registered
every one of those lines would have gone nowhere - after `on_setup()` too, so a
site's own sinks catch them. And it takes the parameter load (hundreds of storage
reads, tens of milliseconds of flash) out of the first beat, where it used to read as
a boot overrun against a 10 ms budget.

## The per-tick cycle

`loop()` is non-virtual, so the order cannot drift between targets. It is not a list
of calls any more but a **phase-major dispatch** over a task table
(`tasks/tasks.h`, registered once in `_register_tasks()`):

```
tick()                          gate the beat; republish the render clock
now_ms = uptime_ms()            one 64-bit "now" for every task this beat
run_phase(PHASE_INPUT)          console/monitor RX; expire_stale, reject_implausible
run_phase(PHASE_CONFIG)         params: the config image everything below reads
run_phase(PHASE_SERVICE)        health, the control latch, network / NTP / MQTT
run_phase(PHASE_CONTROL)        the scan - the control programs, and on_control()
run_phase(PHASE_OUTPUT)         the field write
run_phase(PHASE_PRESENT)        logger refill, console render, the two TX halves
watchdog kick                   only while healthy
```

Two axes, deliberately separate: a task's **phase** is where it sits in the data
flow, written out as code because it must not become configurable, and its
**rate** is its group. See @ref dev_tasks for the groups and what sets each one.

Every task gets the same fresh 64-bit `now_ms`. The order is load-bearing:

- **RX at the top, TX at the bottom.** `BufferedSerial` is ONE `Component`
  registered in two phases: `on_input()` takes the bytes in, `on_present()` puts
  them out. Two tasks, so each half keeps its own cost on the TASKS page, but one
  object and one lifecycle. Stepping the whole pump at both ends would cost an
  extra read per beat and split the console's baud-paced TX across the render.
- **Inputs age out before anything reads**, so one beat is coherent - every task sees
  the same instant. `expire_stale()` catches a reading nothing refreshed;
  `reject_implausible()` catches one that is fresh and still nonsense (a dead 4-20 mA
  loop reads full scale, which no timeout notices). Field drivers slot in here.
- **Health before the logger drains**, so an alarm transition is logged in the same
  beat it happens, and before EXECUTE, so a fault inhibits *this* beat's programs.
- **The scan runs only while control is enabled**, and withdrawing control is a
  *latch* rather than a per-beat sweep - @ref dev_scan has the rule and its two
  independent inhibits.
- **The watchdog kick is withheld deliberately** when unhealthy, and the outputs are
  dropped to safe state first. The reset itself gives no such guarantee, and the gap
  between the last kick and the reboot is exactly when an actuator must not be left
  energised.

## Components and contexts

**`Component`** (`core/component.h`) is the mixin every subsystem derives. It
gives each a one-shot BOOT/DEBUG announce on its first `step()`, so a subsystem
missing from the loop is obvious in the log rather than silently inert, an
optional per-component rate, and a watched config serial.

It has **one hook per phase** - `on_input()` ... `on_present()` - and a component
overrides the one whose place in the data flow matches its work. Most override
exactly one. A component that genuinely spans the beat overrides several and is
registered once per phase (`add_component<PHASE_INPUT>(...)`): the field-bus
master commits answers at INPUT and issues the next request at OUTPUT, and the
TCP slave decodes at INPUT, persists at SERVICE and answers at PRESENT.

`step()` is non-virtual and fixes the order - due check, first-step `setup()` and
`announce()`, `on_param_changed()` on a serial change, then the phase hook. The
lifecycle is per BEAT, not per phase: a component registered twice would
otherwise consume its activation at the first phase and sit out the second, so
the first dispatch of a beat decides and the rest reuse the verdict.

**`SystemContext`** (`core/context.h`) is a borrowed-pointer aggregate the read-only
paths share, so the UI takes one handle instead of a pointer per subsystem:

```cpp
struct SystemContext {
    Scheduler::SchedulerContext* scheduler;
    Vars::Bank*                  vars;
    const Network::NetworkHandler* network;   // FEATURE_NETWORK
    const Network::NtpClient*      ntp;       // FEATURE_NTP
    const Network::MqttClient*     mqtt;      // FEATURE_MQTT
};
```

Every member may be null - each is compiled in only under its feature flag, and a
consumer must handle its absence. The owner fills it once every subsystem's address
is known, then hands it to the console.

@note **Wiring a new consumer means reading the context, not adding a setter for it.**
`vars` in particular is the one `Vars::Bank` everything gets, sets and overrides
through; nothing should be handed its own pointer to it.

Subsystems publish status by writing a plain struct, not by being called into.
`SchedulerContext` (@ref dev_scheduler) is written in place by the scheduler and read
by the status bar and `stats`; the network clients are read as themselves.
