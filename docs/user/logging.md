# Logging {#user_logging}

The logging system supports multiple sinks. Every important thing the firmware does
is logged and delivered to every sink. The base library provides up to five sinks :
 - the LOGS page
 - the live tail on HOME
 - the SHELL page when it's active
 - the MONITOR serial port
 - the MQTT broker


## Reading a line

```
14:02:31 [!] ALARM_BEG: TOO_HOT active
│        │   │            └ message
│        │   └ topic
│        └ level
└ timestamp
```

**Levels**, least to most severe:

| Glyph | Level    | Typically                               |
|-------|----------|-----------------------------------------|
| `[~]` | debug    | debugging information                   |
| `[-]` | info     | normal progress                         |
| `[+]` | notice   | something worth noticing                |
| `[!]` | warning  | degraded but still running              |
| `[*]` | error    | something failed                        |
| `[@]` | critical | you need to know now                    |

**Topics** say which part of the firmware spoke: `BOOT`, `NETWORK`, `TIME`,
`STATUS`, `HEALTH`, `STORAGE`, `MODBUS`, `ASSERT`, and the alarm transitions
`ALARM_BEG`, `ALARM_END`, `ALARM_FLAP`. One topic is yours: `USER`.

## Writing your own entry

`log <level> <text>` puts a line of your own in the record, on the `USER` topic,
through every sink the firmware's own entries go to:

```
botanik> log notice replaced zone 2 sensor, trim pending
```

It is the way to leave the *why* beside the evidence - the log already shows what
the plant did, and this is where you say what you did to it. For anything that
should outlive the log's circular buffer, use the NOTES page instead
(@ref user_console).

## Choosing how much

Two filters in series. The `sys_log_level` **parameter** is the master floor -
below it nothing is emitted at all, to any sink:

```
botanik> vars info sys_log_level             # report the current threshold
sys_log_level = debug
...
Range ....................... : debug|info|notice|warning|error|critical

botanik> params set sys_log_level warning      # quieten everything below warnings
```

It reads and writes as a **word**, and `get` lists the whole vocabulary on its
`Range` line, so there is no numbering to look up. Anything that is not one of
those words is refused rather than stored - there is no rounding to a nearby
level, because a level is not a quantity.

It is an ordinary parameter otherwise: the PARAMS page and a Modbus master reach
the same setting, and like every parameter it is live at once and durable only
after `params save sys_log_level`. `BOTANIK_LOG_MIN_LEVEL` is the factory value.

@note Over Modbus it is the level's own **code** that travels - 1 debug, 2 info,
4 notice, 8 warning, 16 error, 32 critical. Those codes are also what sits in
already-written log records, so they never get renumbered.

That factory value is `debug`, so the LOGS page can be the complete record. Each
**sink** then applies its own floor on top, and each of those is a parameter too:

| Sink | Parameter | Factory | Why |
|---|---|---|---|
| HOME | `console_home_level` | `notice` | a landing screen you glance at - only what is worth looking up for |
| SHELL | `console_shell_level` | `info` | you are already working here, so keep the running commentary |
| LOGS | `console_logs_level` | `debug` | this page is the record |
| monitor port | `monitor_level` | `debug` | the port exists to be the full record (@ref user_monitor) |
| MQTT | `mqtt_level` | `debug` | every entry costs a publish, so this is the floor to raise first |

A sink floor can only ever be *more* restrictive: raising the master floor to
`warning` quietens every sink, but lowering it to `debug` does **not** make
debug lines appear on HOME. Criticals clear every floor. To silence only the
SHELL page's interleaved log entirely, use `logs off`, which is a different
thing from moving its floor - it is not saved and not a setting.

All six take effect on the next entry, with no group serial to bump:

```
botanik> params set mqtt_level warning   # stop paying to publish the chatter
botanik> params save mqtt_level          # ...and keep it that way
```

The `BOTANIK_*_LOG_MIN_LEVEL` macros are now only the factory values behind
these rows - what a site pins for its hardware, and what a wiped store comes
back at.

## Long entries

An entry too long for one line is written with an explicit break, and the second
line is indented under a `>`:

```
[@] HEALTH: control disabled
                        > outputs at their safe state, scan inhibited
```

On the monitor port and in MQTT payloads the break is folded to ` | ` instead,
so one entry stays one greppable record.

## Rate limiting, and what it tells you

The firmware rate-limits its own logging - globally and per topic - so a storm of
repeats cannot flood a 4800-baud link or drown out everything else. Suppressed
entries are counted, and the count is reported as a **critical**, so the report
survives the storm it describes.

You will also see criticals for two other kinds of silent loss: entries dropped at a
full sink, and bytes dropped because a serial port could not keep up. In every case
the message is telling you that something is *missing* from what you are reading.

Criticals themselves are never dropped: if the queue is full a critical evicts the
oldest entry rather than being discarded.

## Where it comes out

Every sink gets the same entries, filtered only by the floors above. The console
pages are the ones you read while working; the **monitor port** is the one to reach
for when the UI is in the way, when you want to capture a session to a file, or
when you need the log faster than the slow console link can carry it. See
@ref user_monitor.
