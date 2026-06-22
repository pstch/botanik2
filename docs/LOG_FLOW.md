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

\dot "the dual token-bucket: an entry must take a token from both its topic bucket and the global bucket"
digraph rate_limit {
  stylesheet="graphs.css";
  bgcolor="transparent"; rankdir=LR; nodesep=0.35; ranksep=0.5;
  node [shape=box, style="rounded,filled", fontname="Helvetica", fontsize=9, margin="0.1,0.06"];
  edge [arrowsize=0.7, fontname="Helvetica", fontsize=8];

  entry [class="neutral", label="log entry\n(topic T)"];
  gb [class="logic", label="global bucket"];
  tb [class="logic", label="topic[T] bucket"];
  gate [shape=diamond, style=filled, class="event", label="token in\nBOTH?"];
  pass [class="ui", label="push_entry()\nfan-out to writers"];
  drop [class="intercept", label="drop + count\n(periodic CRITICAL\nsuppressed report)"];

  refill [shape=plaintext, style="none", fillcolor="none", label=<<font color="#888888" point-size="8">step() refills<br/>both per period</font>>];

  entry -> gate;
  gb -> gate [style=dashed, label="take 1"];
  tb -> gate [style=dashed, label="take 1"];
  gate -> pass [label="yes"];
  gate -> drop [label="no (either empty)"];
  refill -> gb [style=dotted, arrowsize=0.5, color="#aaaaaa"];
  refill -> tb [style=dotted, arrowsize=0.5, color="#aaaaaa"];
  { rank=same; gb; tb; }
}
\enddot

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
  stylesheet="graphs.css";
  bgcolor="transparent"; rankdir=TB; nodesep=0.35; ranksep=0.5;
  node [shape=plaintext, fontname="Helvetica", fontsize=9];
  edge [arrowsize=0.7, fontname="Helvetica", fontsize=8];

  fw  [shape=box, style="rounded,filled", class="neutral", margin="0.12,0.06", label="firmware code\ninfo() / warning() / ..."];
  plc [shape=box, style="rounded,filled", class="plc",     margin="0.12,0.06", label="PlcLogReader::step()\n(PLC coprocessor ring)"];

  logger [label=<
    <table border="0" cellborder="1" cellspacing="0" cellpadding="5" color="#9e9e9e">
      <tr><td colspan="2" bgcolor="#37474f"><font color="#ffffff"><b>Logger</b></font></td></tr>
      <tr><td bgcolor="#fdebcf">global<br/>bucket</td><td bgcolor="#fdebcf">per-topic<br/>bucket</td></tr>
      <tr><td colspan="2" bgcolor="#fff3cd" align="left"><font color="#555555">pass &#8594; push_entry; empty &#8594; drop + count</font></td></tr>
    </table>
  >];

  mon   [label=<<table border="0" cellborder="1" cellspacing="0" cellpadding="4" color="#9e9e9e"><tr><td bgcolor="#37474f"><font color="#ffffff"><b>Monitor</b></font></td></tr><tr><td bgcolor="#e6dcf0"><font color="#555555">LogWriter</font></td></tr></table>>];
  shell [label=<<table border="0" cellborder="1" cellspacing="0" cellpadding="4" color="#9e9e9e"><tr><td bgcolor="#37474f"><font color="#ffffff"><b>ShellPage</b></font></td></tr><tr><td bgcolor="#e6dcf0"><font color="#555555">writer</font></td></tr></table>>];
  logs  [label=<<table border="0" cellborder="1" cellspacing="0" cellpadding="4" color="#9e9e9e"><tr><td bgcolor="#37474f"><font color="#ffffff"><b>LogsPage</b></font></td></tr><tr><td bgcolor="#e6dcf0"><font color="#555555">writer</font></td></tr></table>>];
  home  [label=<<table border="0" cellborder="1" cellspacing="0" cellpadding="4" color="#9e9e9e"><tr><td bgcolor="#37474f"><font color="#ffffff"><b>HomePage</b></font></td></tr><tr><td bgcolor="#e6dcf0"><font color="#555555">writer</font></td></tr></table>>];

  pagedrain [label=<
    <table border="0" cellborder="1" cellspacing="0" cellpadding="5" color="#9e9e9e">
      <tr><td bgcolor="#37474f"><font color="#ffffff"><b>page LogWriter drain</b> (per tick)</font></td></tr>
      <tr><td bgcolor="#e6dcf0" align="left"><b>active</b> page &#8594; paints to screen</td></tr>
      <tr><td bgcolor="#eeeeee" align="left"><b>background</b> page &#8594; banks into Scrollback</td></tr>
    </table>
  >];

  consbs [label=<<table border="0" cellborder="1" cellspacing="0" cellpadding="4" color="#9e9e9e"><tr><td bgcolor="#37474f"><font color="#ffffff"><b>BufferedSerial</b></font></td></tr><tr><td bgcolor="#d6e4f0">console UART</td></tr></table>>];
  monbs  [label=<<table border="0" cellborder="1" cellspacing="0" cellpadding="4" color="#9e9e9e"><tr><td bgcolor="#37474f"><font color="#ffffff"><b>BufferedSerial</b></font></td></tr><tr><td bgcolor="#d6e4f0">monitor UART</td></tr></table>>];

  fw  -> logger;
  plc -> logger;
  logger -> mon [label="fan-out (push_entry)"];
  logger -> shell;
  logger -> logs;
  logger -> home;
  { rank=same; mon; shell; logs; home; }
  shell -> pagedrain;
  logs  -> pagedrain;
  home  -> pagedrain;
  mon   -> monbs   [label="BufferedLogWriter:\npush -> FIFO -> drain()", fontsize=7];
  pagedrain -> consbs [label="active page\npaints", fontsize=7];
  { rank=same; consbs; monbs; }
}
\enddot

## Format

A formatted line is `TIMESTAMP LEVEL TOPIC MESSAGE` (see
@ref Botanik::Logs::format_log_entry). Levels render as short glyphs (`[-]`
info, `[!]` warning, `[*]` error, `[@]` critical, ...) and `PLC_IO` prints as
`SHARED_IO`.
