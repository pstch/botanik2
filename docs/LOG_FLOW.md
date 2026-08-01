# Log Flow

The output side: how a log entry travels from a call site through rate limiting
to every registered sink. For the *input* side (key events reaching pages) see
[EVENT_DISPATCH.md](EVENT_DISPATCH.md); for the pages that double as sinks, see
[PAGES.md](PAGES.md).

## Sources

An entry enters through @ref Botanik::Logs::Logger, either from:

- **Firmware code** calling a level helper (@ref Botanik::Logs::Logger::info "info()",
  @ref Botanik::Logs::Logger::warning "warning()", etc.), which builds a timestamped
  @ref Botanik::PlcIO::LogEntry_t.
- **The PLC coprocessor**, whose log ring is drained on the main thread by
  @ref Botanik::PlcIO::PlcLogReader and re-emitted through the same `Logger`.

Every entry carries a **level** (`DEBUG`, `INFO`, `NOTICE`, `WARNING`, `ERROR`,
`CRITICAL`) and a **topic** (`SHARED_IO`, `BOOT`, `NETWORK`, `TIME`, `STATUS`, and the
firmware-only `ASSERT`; the `LogTopic` enum in `plcio/vars.h`).

\startuml "LogEntry_t: one log record (PLC-shared wire layout, byte offsets)"
class LogEntry_t {
  timestamp : uint32 @0
  level : LogLevel @4 (uint8)
  topic : LogTopic @5 (uint8)
  length : uint16 @6
  seqNum : uint32 @8
  message : char[LOG_PLC_MESSAGE_LEN] @12
}
\enduml

## Rate limiting

@ref Botanik::Logs::Logger applies a **dual token-bucket** limiter before fan-out: one
global bucket plus one per topic. An entry must take a token from *both* to pass,
otherwise it is dropped and counted. @ref Botanik::Logs::Logger::step "step()" refills
the buckets once per loop period; on a slower decoupled cadence the Logger emits a
per-topic `CRITICAL` summary of how many entries each bucket suppressed - classed
`CRITICAL` so the report survives the storm it describes (see
[Silent-loss reporting](#silent-loss-reporting)).

\startuml "Logger: the rate-limited fan-out hub - dual token buckets, then a fixed writer array"
class Logger {
  debug() / info() / notice() / warning() / error()
  add_writer(LogWriter*) : bool
  watch_tx(BufferedSerial*)
  step()
  --
  _global_bucket : LogBucket_t
  _perTopic_buckets[LOG_TOPIC_COUNT] : LogBucket_t
  _writers[LOG_WRITERS_MAX] : LogWriter*
  _min_level : LogLevel
}
\enduml

```
 log entry (topic T)
        │
        ▼
 ┌───────────────────────────┐   step() refills
 │ global + topic[T] buckets │◀── both per period
 └───────────────────────────┘
        │ take 1 from BOTH?
    ┌───┴───────┐
   yes          no (either empty)
    │            │
    ▼            ▼
 push_entry()   drop + count
 fan-out        (periodic CRITICAL suppressed report)
```

## Sinks

Entries that pass fan out to every registered @ref Botanik::Logs::LogWriter, of two
kinds:

- **Serial** - @ref Botanik::Logs::SerialLogWriter writes the formatted line to a
  `BufferedSerial` port. Its @ref Botanik::Logs::MonitorLogWriter variant
  self-suppresses when the output is an interactive terminal, so it can't garble a
  shared console.
- **Page** - @ref Botanik::Pages::ShellPage, @ref Botanik::Pages::LogsPage and
  @ref Botanik::Pages::HomePage surface entries in the UI.

All derive from @ref Botanik::Logs::BufferedLogWriter, whose `push()` queues into a
FIFO and `drain()` flushes incrementally across calls so nothing blocks. On overflow a
normal entry is dropped and counted, but a `LOG_CRITICAL` entry is **undroppable**: it
evicts the oldest queued entry to seat itself (still counting the eviction as a drop).
`drain()` consumes the read index at format time, decoupling the outgoing line from the
queue so eviction is safe.

\startuml "the LogWriter sink hierarchy: the serial writer and the three pages that double as sinks, all fed by the Logger"
class LogWriter
class BufferedLogWriter
class SerialLogWriter

LogWriter <|-- BufferedLogWriter
BufferedLogWriter <|-- SerialLogWriter
BufferedLogWriter <|-- LogsPage
BufferedLogWriter <|-- HomePage
BufferedLogWriter <|-- ShellPage

Logger o--> "0..LOG_WRITERS_MAX" LogWriter : fans out to
\enduml

## Silent-loss reporting {#silent-loss-reporting}

Three ways an entry can vanish without trace are made loud, each reported
edge-triggered (first event instant, then a per-category holdoff) and classed
`LOG_CRITICAL` so the undroppable path above guarantees delivery:

- **Sink drops** - entries dropped at a writer's full queue.
  @ref Botanik::Logs::LogWriter::loss_count "loss_count()" is summed across writers
  in `step()` (holdoff `LOG_SINK_DROP_REPORT_PERIOD_MS`, 5 s).
- **TX-full truncation** - bytes a `BufferedSerial` couldn't fit
  (`tx_dropped()`); the **console** port is registered via
  @ref Botanik::Logs::Logger::watch_tx "watch_tx()" and polled in `step()`
  (holdoff `LOG_TX_DROP_REPORT_PERIOD_MS`, 10 s).
- **Rate-limiter suppressions** - the per-topic bucket summary described above.

## Pipeline

```
 firmware code (info/warning/...)      PlcLogReader::step() (PLC ring)
              └─────────────┬────────────────────┘
                            ▼
         Logger  (global + per-topic buckets;
                  pass → push_entry, empty → drop + count)
                            │ fan-out (push_entry)
        ┌──────────┬────────┴────┬──────────┐
        ▼          ▼             ▼          ▼
     Monitor    ShellPage     LogsPage   HomePage
    (LogWriter)    └───────────┬──────────┘
        │                      ▼
        │          page LogWriter drain (per tick):
        │            active page     → paints to screen
        │            background page → banks into Scrollback
        ▼                      ▼
 BufferedSerial          BufferedSerial
 monitor UART            console UART
 (push→FIFO→drain)       (active page paints)
```

## Format

A formatted line is `TIMESTAMP LEVEL TOPIC MESSAGE` (see
@ref Botanik::Logs::format_log_entry). Levels render as short glyphs (`[-]`
info, `[!]` warning, `[*]` error, `[@]` critical, ...) and each topic prints as
its display label (see @ref Botanik::Logs::TOPIC_NAMES).
