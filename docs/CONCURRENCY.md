# Concurrency model

This firmware (the "C side", running on the OPTA's **M7** core) is deliberately
single-threaded with a small, well-defined set of asynchronous boundaries. Keeping
that surface tiny is what lets the codebase use plain `volatile` + memory barriers
instead of `std::atomic` or locks.

## One cooperative loop

All of our code runs in a single fixed-period main loop (`LoopScheduler`, period
`Config::TASK_PERIOD_MS`). Every subsystem exposes a non-blocking `step()` and is
called once per beat in a fixed order - the **per-tick cycle contract**:

\dot "the per-tick cycle contract (one beat, repeating every TASK_PERIOD_MS)"
digraph per_tick {
  stylesheet="graphs.css";
  bgcolor="transparent"; rankdir=TB; nodesep=0.28; ranksep=0.6;
  node [shape=box, style="rounded,filled", fontname="Helvetica", fontsize=9, margin="0.08,0.05"];
  edge [arrowsize=0.7];

  tick   [label="LoopScheduler\ntick()",      class="scheduler"];
  recv   [label="BufferedSerial\nreceive()",  class="serial"];
  plc    [label="PlcSyncHandler\nstep()",     class="plc"];
  plcr   [label="PlcLogReader\nstep()",       class="plc"];
  health [label="HealthMonitor\nstep()",      class="logic"];
  onloop [label="site\non_loop()",            class="logic"];
  logger [label="Logger\nstep()",             class="ui"];
  term   [label="AnsiTerminal\nstep()",       class="ui"];
  mon    [label="PerfMonitor\ndrain()",       class="ui"];
  tx     [label="BufferedSerial\ntransmit()", class="serial"];
  wd     [label="HAL\nwatchdog_kick()",       class="scheduler"];

  // Two rows, snaking: row 1 left-to-right, row 2 right-to-left (dir=back so its
  // arrows follow the real flow). watchdog_kick lands bottom-left directly under
  // tick, so the "next beat" loop-back is a short straight edge up the left margin
  // that crosses nothing.
  { rank=same; tick -> recv -> plc -> plcr -> health -> onloop [weight=10]; }
  { rank=same; wd -> tx -> mon -> term -> logger [dir=back, weight=10]; }
  onloop -> logger;   // drop on the right
  wd -> tick [label="next beat", style=dashed, constraint=false, fontname="Helvetica", fontsize=8];

  legend [shape=plaintext, label=<
    <table border="0" cellborder="0" cellspacing="2">
      <tr><td bgcolor="#e0e0e0">&#160;&#160;&#160;</td><td align="left">scheduler / watchdog</td></tr>
      <tr><td bgcolor="#d6e4f0">&#160;&#160;&#160;</td><td align="left">serial I/O</td></tr>
      <tr><td bgcolor="#d7ecd9">&#160;&#160;&#160;</td><td align="left">PLC sync</td></tr>
      <tr><td bgcolor="#fdebcf">&#160;&#160;&#160;</td><td align="left">logic</td></tr>
      <tr><td bgcolor="#e6dcf0">&#160;&#160;&#160;</td><td align="left">UI / logging</td></tr>
    </table>
  >];
  wd -> legend [style=invis];
}
\enddot

The order is load-bearing: inputs are ingested before processing; `health` runs once
the PLC payload is fresh (so it asserts the platform-health alarms and advances the
alarm model this beat) and before the logger drains (so an alarm START/END is logged
this beat); the serial TX rings are flushed only after every subsystem has queued its
output. This loop lives in `BotanikFirmware::loop()` (one definition, shared by both
composition roots), so the order can't drift between targets.

**Two time bases, one clock.** `tick()` republishes the 32-bit
`SchedulerContext::timestamp_ms` (= low 32 bits of `HAL::uptime_ms()`), the shared
"now" for the **render path** (terminal, pages) where ages are wrap-projected for
display. The **health / alarm / on_loop** steps instead take a fresh 64-bit
`HAL::uptime_ms()`: their fault-grace and alarm-latch timers want a wrap-free
monotonic clock, and the few microseconds between `tick()` and that read are
immaterial at their second scale. Both are the same underlying monotonic clock.

## The only async boundaries

1. **PLC shared memory (M7 <-> M4).** The single genuine concurrency in our code.
   No hardware cache coherency, and the PLC runtime is assumed to use unordered
   accesses, so this boundary is governed by: the seqnum/ack **handshake contract**,
   a torn-read **diagnostic**, `volatile` counter reads (`HAL::ordered_read`), DSB/DMB
   barriers, and M7 D-cache clean/invalidate. See `docs/PLC_SYNC.md`.

2. **The hardware watchdog.** Fire-and-forget: a timer that resets the MCU unless
   `watchdog_kick()` runs each healthy beat. It shares no mutable state with our
   code beyond the kick, so it needs no synchronisation.

That's it. In particular there are **no serial ISRs in our code**:
`BufferedSerial::receive()`/`transmit()`/`step()` run on the main loop (the Arduino
UART driver keeps its own ISR *below* the HAL, filling its own buffer). The
`BufferedSerial` ring and the `BufferedLogWriter` queue are therefore **not
ISR-safe** and must only be touched from the main loop - they are marked as such in
their headers.

## The discipline (no std::atomic)

Because the async surface is just the PLC shared memory, cross-core access uses:

- `HAL::ordered_read<T>` - `volatile` load + `dmb()` for polled cross-core counters
  (stops the compiler caching the other core's writes; orders the read).
- `HAL::ordered_copy`/`ordered_move` - word-granular, `dsb()`-fenced block/scalar
  moves of shared payloads.
- `HAL::cache_clean`/`cache_invalidate` - M7 D-cache maintenance by region (no-ops
  where there is no D-cache, e.g. the simulator or a Cortex-M4).
- `Utils::Millis32` - a strong type for the wrap-prone 32-bit loop clock whose only
  operation is wrap-safe elapsed subtraction, so it can't be mixed with the
  monotonic 64-bit `HAL::uptime_ms()`.

The published `timestamp_ms` is 32-bit and wraps every ~49 days; every consumer
compares it with wrap-safe `(uint32_t)(now - then)` subtraction, never
`now > then + delta`.
EOF
echo "created docs/CONCURRENCY.md"