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
`CRITICAL`) and a **topic** (`PLC_IO`, `BOOT`, `NETWORK`, `TIME`, `STATUS`, and the
firmware-only `ASSERT`; the `LogTopic` enum in `plcio/vars.h`).

## Rate limiting

@ref Botanik::Logs::Logger applies a **dual token-bucket** limiter before
fan-out: one global bucket plus one bucket per topic. An entry must take a token
from *both* its topic bucket and the global bucket to pass; otherwise it is
dropped and counted. The bucket array is bounded by the real topic count
(`LOG_TOPIC_COUNT`), so the per-tick refill scan stays cheap.
@ref Botanik::Logs::Logger::step "step()" refills the buckets once per loop
period, and on a slower, decoupled cadence the Logger emits a per-topic
`CRITICAL` summary of how many entries each bucket suppressed - classed
`CRITICAL` so the report survives the storm it describes (see
[Silent-loss reporting](#silent-loss-reporting)).

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
FIFO and `drain()` flushes incrementally, resuming a partial send across calls so
nothing blocks. On overflow a normal entry is dropped and counted, but a
`LOG_CRITICAL` entry is **undroppable**: it evicts the oldest queued entry to seat
itself (still counting the eviction as a drop). `drain()` consumes the read index
at format time, which decouples the outgoing line from the queue and makes that
eviction safe. The pipeline below shows where each sink is drained.

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

\dot "a log entry: sources -> Logger (rate limiting) -> every registered writer -> its sink"
digraph logflow {
  bgcolor="transparent"; rankdir=TB; nodesep=0.35; ranksep=0.5;
  node [shape=box, style="rounded,filled", fontname="Helvetica", fontsize=9, margin="0.12,0.06"];
  edge [arrowsize=0.7, fontname="Helvetica", fontsize=8];

  fw  [label="firmware code\ninfo() / warning() / ...", fillcolor="#eceff1"];
  plc [label="PlcLogReader::step()\n(PLC coprocessor ring)", fillcolor="#d7ecd9"];
  logger [label="Logger\nglobal bucket  x  topic bucket\n(drop + count on empty;\nperiodic suppressed report)", fillcolor="#fdebcf"];

  mon   [label="Monitor\nLogWriter", fillcolor="#e6dcf0"];
  shell [label="ShellPage", fillcolor="#e6dcf0"];
  logs  [label="LogsPage", fillcolor="#e6dcf0"];
  home  [label="HomePage", fillcolor="#e6dcf0"];

  monser  [label="monitor serial\n(drain() in main loop)", fillcolor="#d6e4f0"];
  pageout [label="active page paints; background\npages bank into Scrollback\nfor focus() replay", fillcolor="#d6e4f0"];

  fw  -> logger;
  plc -> logger;
  logger -> mon [label="fan-out (push_entry)"];
  logger -> shell;
  logger -> logs;
  logger -> home;
  { rank=same; mon; shell; logs; home; }
  mon   -> monser  [label="BufferedLogWriter:\npush -> FIFO -> drain()", fontsize=7];
  shell -> pageout;
  logs  -> pageout;
  home  -> pageout;
}
\enddot

## Format

A formatted line is `TIMESTAMP LEVEL TOPIC MESSAGE` (see
@ref Botanik::Logs::format_log_entry). Levels render as short glyphs (`[-]`
info, `[!]` warning, `[*]` error, `[@]` critical, ...) and `PLC_IO` prints as
`SHARED_IO`.
