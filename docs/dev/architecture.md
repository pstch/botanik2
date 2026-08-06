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
| `core/` | `BotanikFirmware`, `Component`, `SystemContext`, the feature-gated stacks | this page |
| `scheduler/` | `LoopScheduler`, the fixed-period driver and its self-instrumentation, plus the `Scheduler::Registry` the beat is dispatched from and its per-task cost sampling | @ref dev_scheduler |
| `vars/` | the variable model: descriptors, value slots, `Bank`, parameters | @ref dev_vars |
| `scan/` | `Scan::Driver`: which control program is due, and running it | @ref int_tasks |
| `plc/` | the POU library the programs are written in: `functions/` (stateless) and `blocks/` (stateful) | @ref int_tasks |
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

```
set_assert_logger      route assert failures to the log
io.init()              seed every variable with its declared default
begin UARTs            console, monitor
register core sinks    monitor writer, shell, logs, home-page tail; TX-drop watches
set_diagnostics        let the shell reach the live logger and alarm model
register_commands()    ─┐
register_pages()        ├─ site hooks
setup_terminal()        │
switch_to(0)           ─┘
boot banner
MQTT sinks + register_telemetry()
on_setup()             ← site post-wiring
tasks.setup()          ← every task's one-time init: announce(), POST, param load
Watchdog::start()
io.enable_control()    ← the field is ours from here, and not before
```

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
(`scheduler/tasks.h`, registered once in `_register_tasks()`):

```
tick()                          gate the beat; republish the render clock
now_ms = uptime_ms()            one 64-bit "now" for every task this beat
run_phase(PHASE_INPUT)          console/monitor RX; expire_stale, reject_implausible
run_phase(PHASE_CONFIG)         params: the config image everything below reads
run_phase(PHASE_SERVICE)        health, the control latch, network / NTP / MQTT
run_phase(PHASE_EXECUTE)        the scan - the control programs, and on_step()
run_phase(PHASE_OUTPUT)         the field write
run_phase(PHASE_PRESENT)        logger refill, console render, the two TX halves
watchdog kick                   only while healthy
```

Two axes, deliberately separate. A task's **phase** is where it sits in the data
flow, and the phase order above is written out as code because it must not become
configurable. A task's **rate** is its `GROUP_FAST` / `_MAIN` / `_SLOW` /
`_BACKGROUND` group - a property of the task, so retuning one never silently
reorders the cycle. A phase runs the tasks registered to it whose group has come
due; a not-due task costs two comparisons.

Every task gets the same fresh 64-bit `now_ms`. The order is load-bearing:

- **RX at the top, TX at the bottom.** `BufferedSerial` is a `Component`, but the
  loop registers its two halves as separate tasks at the phases they belong to.
  Stepping the whole pump at both ends would cost an extra read per beat and split
  the console's baud-paced TX across the render.
- **Inputs age out before anything reads**, so one beat is coherent - every task sees
  the same instant. `expire_stale()` catches a reading nothing refreshed;
  `reject_implausible()` catches one that is fresh and still nonsense (a dead 4-20 mA
  loop reads full scale, which no timeout notices). Field drivers slot in here.
- **Health before the logger drains**, so an alarm transition is logged in the same
  beat it happens, and before EXECUTE, so a fault inhibits *this* beat's programs.
- **The scan runs only while control is enabled.** Withdrawing control is a *latch*,
  not a per-beat sweep: sweeping outputs after the scan would flicker, because the
  next beat's tasks re-drive the same demands before the sweep runs again. The latch
  carries two independent inhibits (operator and health), and the health edge moves
  only its own - so a recovery hands control back without overruling an operator who
  disabled it by hand. See @ref user_control.
- **The watchdog kick is withheld deliberately** when unhealthy, and the outputs are
  dropped to safe state first. The reset itself gives no such guarantee, and the gap
  between the last kick and the reboot is exactly when an actuator must not be left
  energised.

## Components and contexts

**`Component`** (`core/component.h`) is a mixin giving each subsystem a one-shot
BOOT/DEBUG announce on its first `step()`, so a subsystem missing from the loop is
obvious in the log rather than silently inert.

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
