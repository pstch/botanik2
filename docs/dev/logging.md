# Log flow {#dev_logging}

The output side: how a log entry travels from a call site through rate limiting to
every registered sink. For the *input* side see @ref dev_input; for the pages that
double as sinks, see @ref dev_pages. The operator-facing view - levels, topics, the
monitor port - is @ref user_logging.

## Sources

An entry enters through @ref Botanik::Logs::Logger when firmware code calls a level
helper (@ref Botanik::Logs::Logger::info "info()",
@ref Botanik::Logs::Logger::warning "warning()", …), which builds a timestamped
record carrying a **level** and a **topic** - the `LogLevel` / `LogTopic` enums in
`logs/topics.h`.

@note The topic numbering is deliberately not compacted. It is what sits in
already-written log records, so a gap stays a gap.

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

Entries that pass fan out to every registered @ref Botanik::Logs::LogWriter, of three
kinds:

- **Serial** - @ref Botanik::Logs::SerialLogWriter writes the formatted line to a
  `BufferedSerial` port. Constructed with `drop_if_tty`, it decides **once** at
  construction whether the port is an interactive terminal and self-suppresses if so,
  since that means it shares a screen with the UI. This is why the simulator's
  monitor stream is silent until stderr is redirected.
- **Page** - @ref Botanik::Pages::ShellPage, @ref Botanik::Pages::LogsPage and
  @ref Botanik::Pages::HomePage surface entries in the UI.
- **MQTT** - @ref Botanik::Network::MqttLogWriter publishes each entry to the broker
  under `<prefix>/<log topic>/<level>/<topic>`, best-effort and with its own minimum
  level (`MQTT_LOG_MIN_LEVEL`).

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
 firmware code (info/warning/...)
                            │
                            ▼
         Logger  (global + per-topic buckets;
                  pass → push_entry, empty → drop + count)
                            │ fan-out (push_entry)
     ┌──────────┬───────────┼───────────┬──────────┐
     ▼          ▼           ▼           ▼          ▼
  Monitor    ShellPage   LogsPage   HomePage   MqttLogWriter
 (LogWriter)    └──────────┬─────────────┘          │
     │                     ▼                        ▼
     │         page LogWriter drain (per tick):   MqttClient
     │           active page     → paints to screen
     │           background page → banks into Scrollback
     ▼                     ▼
 BufferedSerial      BufferedSerial
 monitor UART        console UART
 (push→FIFO→drain)   (active page paints)
```

## Format

A formatted line is `TIMESTAMP LEVEL TOPIC MESSAGE` (see
@ref Botanik::Logs::format_log_entry). Levels render as short glyphs (`[-]`
info, `[!]` warning, `[*]` error, `[@]` critical, ...) and each topic prints as
its display label (see @ref Botanik::Logs::TOPIC_NAMES).
