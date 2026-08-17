# Tasks {#user_tasks}

Everything the firmware does in a beat is a **task**: a name, a rate, a place in
the beat, and one piece of work. The TASKS page and the `tasks` command list
them, and between them they answer the two questions worth asking of a unit that
feels slow - *what is this thing spending its time on*, and *is anything failing
to keep up*.

This page describes the tasks the **library** registers. A site adds its own on
top - its control programs, and any component it registers itself - and they
appear in exactly the same lists.

## Rate and phase

Two things are recorded about every task, and they are independent.

**Phase** is where in the beat it sits. Six of them, always in this order, and it
is the data flow: readings come in before anything reads them, control runs after
the configuration and the health check, and what control decided goes out to the
field afterwards.

| Phase | For |
|---|---|
| `input` | take the field and the wire in, then age out what has gone stale |
| `config` | apply settings, so everything below reads one coherent configuration |
| `service` | supervision and the background state machines: health, alarms, network |
| `control` | the control programs, and only these are stopped when control authority is withdrawn |
| `output` | hand what control decided to the field |
| `present` | report what just happened: logs, the screen, the answers to a SCADA master |

**Rate** is how often it runs - one of four groups, so a rate is retuned in one
place for everything that shares it:

| Group | Every |
|---|---|
| `fast` | 10 ms - the beat itself |
| `main` | 50 ms |
| `slow` | 100 ms |
| `bkgnd` | 500 ms |

A slower rate is not a lesser task. The variable expiry sweep is `bkgnd` because
it resolves timeouts measured in seconds; running it faster would cost CPU and
change nothing.

## Reading the list

```
botanik> tasks stats
Registered ....... : 23       (0 refused, 48 slots)
Beat ............. : 10ms     (fast/main/slow/bkgnd 10/50/100/500)
Activations ...... : 11287    (across 23 task(s))
Missed ........... : 0        (every task kept its rate)
```

```
botanik> tasks get supply_watch
Runs in                : control (slow, every 100 ms)
Activations            : 105 run, 0 missed, last 40 ms ago
Cost                   : last 0 us, mean 0 us, peak 0 us
Duty                   : 0.00 % of its own period
```

| Command | Does |
|---|---|
| `tasks stats` | the totals: how many are registered, how many refused, activations, missed |
| `tasks list [<prefix>]` | the table, optionally filtered by name |
| `tasks get <name>` | one task in detail |

The TASKS page shows the same table, name-sorted, with a panel for the selected
row. See @ref user_console.

**`MISSED` is the number to watch.** A task that comes due late runs *once* -
missed activations are dropped, never caught up, because a catch-up burst would
arrive exactly when the unit is already behind. So a rising `MISSED` is the only
record that a task is not running at the rate it declared. `PEAK` never decays
and is a diagnostic; `MISSED` is a symptom.

**`refused` should be zero.** Registrations past the table's capacity are refused
and counted rather than silently dropped; a non-zero count means a task is not in
the beat at all, and is reported as a critical at boot.

## The library's tasks

Twenty-one on a full build, plus one per control program the site declares.
Anything gated on a feature is simply absent when that feature is compiled out -
`features` from the shell says which are in this binary, and `tasks list` is
always the authoritative answer for the unit in front of you.

### Input

| Task | Rate | Does |
|---|---|---|
| `console_rx` | fast | take the console's bytes in and route what they decode to |
| `modbus_rtu_rx` | fast | parse and commit whatever the field bus answered, before anything reads it |
| `modbus_tcp_rx` | fast | accept, decode and apply a SCADA master's writes, before the programs run |
| `expire_stale` | bkgnd | invalidate readings older than their declared timeout |

### Config

| Task | Rate | Does |
|---|---|---|
| `console_cfg` | slow | the console's own: its rate, make and size (the `console_` group) |
| `monitor_cfg` | slow | the monitor port's own rate |
| `params` | slow | the parameter image, the paced write of retained values, and the log level - the one setting whose consumer cannot read it for itself |

### Service

| Task | Rate | Does |
|---|---|---|
| `control_latch` | fast | carry the health verdict to control authority, and the authority edge to the programs |
| `health` | slow | the platform alarms, and the watchdog verdict |
| `alarms` | slow | advance every alarm's dwell and raise its transitions |
| `net` `ntp` `mqtt` | fast | the network, clock-sync and broker state machines |
| `telemetry` | fast | publish what is due to the broker |
| `modbus_tcp_wr` | fast | make a master's writes durable, a couple per beat, so a burst is not a flash storm |

### Control

**Only these stop when control authority is withdrawn.** The phases around them
do not: withdrawing control writes safe states into the image, and something
still has to carry those out to the field. See @ref user_control.

There is no library task here. The rows are the site's control programs, one
each, at the rate each declared - on the reference site, `supply_watch` (slow)
and `supply_report` (bkgnd). Yours will differ, which is the point of the list.

### Output

| Task | Rate | Does |
|---|---|---|
| `modbus_rtu_tx` | fast | issue the next field-bus request, so a demand decided this beat reaches the wire |

### Present

| Task | Rate | Does |
|---|---|---|
| `modbus_tcp_tx` | fast | answer a SCADA master, off the image the scan just produced |
| `console` | slow | draw the screen |
| `console_tx` | fast | reconcile the frame to the wire and keep the UART fed |
| `monitor_log` | fast | the monitor port: its bytes in, the log queue out |

The watchdog kick is not a task. It sits at the very bottom of the beat, after
every phase, because it must mean *this beat completed* - and it is withheld when
the unit is unhealthy, having first dropped the outputs to their safe state.
