# Logging {#user_logging}

Every notable thing the firmware does is logged once and delivered to every sink at
once: the LOGS page, the live tail on HOME, the SHELL page while it is in front, the
out-of-band monitor port, and MQTT where telemetry is enabled.

## Reading a line

```
14:02:31 [!] ALARM_START: TOO_HOT active
│        │   │            └ message
│        │   └ topic
│        └ level
└ timestamp
```

**Levels**, least to most severe:

| Glyph | Level | Typically |
|---|---|---|
| `[~]` | debug | development detail; off by default |
| `[-]` | info | normal progress |
| `[+]` | notice | something worth noticing but not wrong |
| `[!]` | warning | degraded, still running |
| `[*]` | error | something failed |
| `[@]` | critical | you need to know now |

**Topics** say which part of the firmware spoke: `BOOT`, `NETWORK`, `TIME`,
`STATUS`, `HEALTH`, `STORAGE`, `SHARED_IO`, `ASSERT`, and the alarm transitions
`ALARM_START`, `ALARM_END`, `ALARM_FLAP`.

`STORAGE` is its own topic on purpose - a persistence failure must not be buried
under boot chatter.

## Choosing how much

`loglevel` sets the minimum level that is emitted at all:

```
botanik> loglevel            # report the current threshold
loglevel: info
botanik> loglevel warning    # quieten everything below warnings
```

The default is `info`. This applies to every sink, not just the page you are on. To
quieten only the SHELL page's interleaved log, use `logs off` instead.

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

## The monitor port

The unit has a second serial port carrying the log and nothing else.

| | |
|---|---|
| Port | monitor (OPTA: **USB CDC**) |
| Speed | `Config::MONITOR_BAUD`, **115200** by default |
| Content | plain formatted log lines, one per entry |
| Input | none - it is output-only |

This is the port to use when the console UI is in the way, when you want to capture
a session to a file, or when you need the log faster than the slow console link can
carry it. It never competes with the UI for the console's byte budget, so it does not
drop lines the way a busy screen can.

On hardware, plug in a USB cable and open the CDC device at 115200. Everything the
logger emits appears there, at whatever `loglevel` is set.

@note On the simulator the monitor stream goes to **stderr** and suppresses itself
when that is a terminal, so it cannot garble the UI on the same screen. Redirect it
to see it: `./botanik_sim 2> sim.log`.
