# Logging Pipeline

How a log line travels from a `log.info(...)` call to a screen or serial port,
and how the rate limiter keeps a misbehaving caller from flooding the link.

## Layers

### Logger

The front end (`Botanik::Logs::Logger`). The level helpers (`debug`, `info`,
`notice`, `warning`, `error`, `critical`) each build a timestamped `LogEntry_t`
from a printf-style message and hand it to `push_entry()`. From there a
**dual token-bucket** rate limiter decides whether the entry survives:

- a **per-topic** bucket is checked first, so one chatty topic can't drain the
  shared budget on entries that were going to be dropped anyway;
- then the **global** bucket is checked.

Only an entry that takes a token from both buckets is fanned out to the writers.
`step()` (called once per main-loop period) refills the buckets on their own
cadences and, on a slower separate cadence, emits one `"N msgs suppressed"`
summary per bucket that dropped anything — so a sustained storm produces an
occasional note instead of one report per refill.

### LogWriter

The output channels (`Botanik::Logs::LogWriter`). A writer `push()`es entries
into its own queue and `drain()`s them toward its medium; nothing blocks.

| Writer               | Medium                            | Drained by                         |
|----------------------|-----------------------------------|------------------------------------|
| `BufferedLogWriter`  | in-memory FIFO (base class)       | subclass                           |
| `SerialLogWriter`    | a `BufferedSerial` TX ring        | its own `step()` in the main loop  |
| `MonitorLogWriter`   | diagnostic port (stderr on sim)   | self-suppresses on a shared tty    |
| page writers         | a page's scrollback (Shell/Logs)  | the page's render path             |

`BufferedLogWriter` handles queueing, drop counting, and partial-send
resumption: a half-written line picks up where it left off on the next
`drain()` without re-formatting.

## Flow

```
  log.info(TOPIC, "fmt", ...)
        │  build timestamped LogEntry_t
        ▼
 ┌──────────────────────────────────────────────┐
 │                   Logger                       │
 │                                                │
 │   push_entry()                                 │
 │      │                                         │
 │      ▼                                         │
 │   per-topic bucket ──drop──┐                   │
 │      │ token              (suppressed++)       │
 │      ▼                     │                   │
 │   global bucket ─────drop──┤                   │
 │      │ token               │                   │
 │      │            step() drains counters →     │
 │      │            "N msgs suppressed" entry ───┤
 │      ▼                                         │
 │   fan out to every registered writer           │
 └───────┬──────────────┬──────────────┬──────────┘
         ▼              ▼              ▼
   SerialLogWriter  MonitorLogWriter  page writers
   (TX ring)        (diag port)       (scrollback)
         │              │              │
      main loop      main loop      render path
      step()/flush   step()/flush   drain
```

## Entry format

Every writer formats through the shared `format_log_entry()` (`logs/format.h`):

```
TIMESTAMP LEVEL TOPIC: MESSAGE
```

The `LEVEL` is a short bracketed indicator (`[~]` debug … `[@]` critical) and the
`TOPIC:` is omitted entirely for the empty (`NONE`) topic, so a topic-less line
carries no dangling separator. `SerialLogWriter` appends a newline (the page
writers wrap by display width into stored rows instead).

## Topics

Topics are the `Botanik::PlcIO::LogTopic` enum (`BOOT`, `NETWORK`, `TIME`,
`STATUS`, `PLC_IO`, …); each gets its own per-topic bucket. The count is bounded
by `Config::LOG_TOPIC_MAX` (a `uint8_t`, so ≤ 255). Tuning lives in
`libbotanik_config.h` (the `LOG_*` constants).
