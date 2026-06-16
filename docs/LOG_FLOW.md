# Log Flow

The output side: how a log entry travels from a call site through rate limiting
to every registered sink. For the *input* side (key events reaching pages) see
[EVENT_DISPATCH.md](EVENT_DISPATCH.md); for the pages that double as sinks, see
[PAGES.md](PAGES.md).

## Sources

An entry enters through @ref Botanik::Logs::Logger, either from:

- **Firmware code** calling a level helper -
  @ref Botanik::Logs::Logger::info "info()",
  @ref Botanik::Logs::Logger::warning "warning()",
  @ref Botanik::Logs::Logger::error "error()", etc. Each builds a timestamped
  @ref Botanik::PlcIO::LogEntry_t and forwards it to
  @ref Botanik::Logs::Logger::push_entry "push_entry()".
- **The PLC coprocessor**, whose log ring is drained on the main thread by
  @ref Botanik::PlcIO::PlcLogReader, which re-emits each entry through the same
  `Logger`.

Every entry carries a **level** (`DEBUG`, `INFO`, `NOTICE`, `WARNING`, `ERROR`,
`CRITICAL`) and a **topic** (`BOOT`, `NETWORK`, `TIME`, `STATUS`, `PLC_IO`).

## Rate limiting

@ref Botanik::Logs::Logger applies a **dual token-bucket** limiter before
fan-out: one global bucket plus one bucket per topic. An entry must take a token
from *both* its topic bucket and the global bucket to pass; otherwise it is
dropped and counted. @ref Botanik::Logs::Logger::step "step()" refills the
buckets once per loop period, and on a slower, decoupled cadence the Logger emits
a synthetic `WARNING` summarizing how many entries each bucket suppressed.

## Sinks

Entries that pass fan out to every registered @ref Botanik::Logs::LogWriter
(`add_writer()` in `main.cpp`), of two kinds:

- **Serial** - @ref Botanik::Logs::SerialLogWriter writes the formatted line to a
  `BufferedSerial` port. Its @ref Botanik::Logs::MonitorLogWriter variant
  self-suppresses (drops at `push()`) when the output is an interactive terminal,
  so it can't garble a shared console.
- **Page** - @ref Botanik::Pages::ShellPage, @ref Botanik::Pages::LogsPage and
  @ref Botanik::Pages::HomePage surface entries in the UI.

All derive from @ref Botanik::Logs::BufferedLogWriter, whose `push()` queues into a
FIFO (dropping + counting on overflow) and `drain()` flushes incrementally,
resuming a partial send across calls so nothing blocks. The pipeline below shows
where each sink is drained.

## Pipeline

```
  firmware code                     PLC coprocessor
  info()/warning()/…                  (log ring)
        │                                 │
        │                                 ▼
        │                          PlcLogReader::step()
        │                                 │
        └────────────────┬────────────────┘
                         ▼
                 ┌───────────────────────────────────────┐
                 │              Logger                    │
                 │  ┌─────────────────────────────────┐  │
                 │  │  global bucket  ⨯  topic bucket │  │  ← dual token-bucket
                 │  └───────────────┬─────────────────┘  │     (drop + count on empty;
                 │     push_entry() │                     │      periodic suppressed report)
                 └──────────────────┼─────────────────────┘
                                    │ fan-out to every registered LogWriter
        ┌───────────────────┬───────┴───────────┬───────────────────┐
        ▼                   ▼                   ▼                   ▼
 ┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌──────────────┐
 │ Monitor     │    │ ShellPage   │    │ LogsPage    │    │ HomePage     │
 │ LogWriter   │    │ (writer)    │    │ (writer)    │    │ (writer)     │
 └──────┬──────┘    └──────┬──────┘    └──────┬──────┘    └──────┬───────┘
        │                  │                  │                  │
 BufferedLogWriter: push() → FIFO queue → drain() (incremental, non-blocking)
        │                  │                  │                  │
        ▼                  └──────────────────┴──────────────────┘
  monitor serial                  drained from tick():
  (drain() in main loop)   active page paints; background pages
                           bank into Scrollback for focus() replay
```

## Format

A formatted line is `TIMESTAMP LEVEL TOPIC MESSAGE` (see
@ref Botanik::Logs::format_log_entry). Levels render as short glyphs (`[-]`
info, `[!]` warning, `[*]` error, ...) and `PLC_IO` prints as `SHARED_IO`.
