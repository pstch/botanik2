# PLC Shared-Memory Sync

How the host (the M7 core running this firmware) exchanges data with the PLC
coprocessor (the M4 core) over a shared-memory region, without assuming the PLC
runtime uses ordered memory accesses.

## Shared memory layout

Two regions, each a `PlcInput<Data>` / `PlcOutput<Data>` (`plcio/vars.h`). "Input"
and "Output" are named from the **PLC's** point of view: the host *writes* the
input region and *reads* the output region.

\dot "The two shared regions: the host writes Input and reads Output"
digraph plc_regions {
  bgcolor="transparent";
  node [shape=plaintext, fontname="Helvetica", fontsize=10];

  in [label=<
    <table border="0" cellborder="1" cellspacing="0" cellpadding="6" color="#9e9e9e">
      <tr><td colspan="2" bgcolor="#37474f"><font color="#ffffff"><b>PlcInput&lt;InputData&gt;</b></font></td></tr>
      <tr><td colspan="2" bgcolor="#455a64"><font color="#cfd8dc">host &#8594; PLC</font></td></tr>
      <tr><td bgcolor="#d6e4f0"><b>sync</b></td><td bgcolor="#d6e4f0" align="left">rxSeqNum &#160; txAckNum</td></tr>
      <tr><td bgcolor="#fdebcf"><b>logs</b></td><td bgcolor="#fdebcf" align="left">readIndex</td></tr>
      <tr><td bgcolor="#eeeeee"><b>data</b></td><td bgcolor="#eeeeee" align="left"><font color="#666666">&lt;site payload&gt;</font></td></tr>
    </table>
  >];

  out [label=<
    <table border="0" cellborder="1" cellspacing="0" cellpadding="6" color="#9e9e9e">
      <tr><td colspan="2" bgcolor="#37474f"><font color="#ffffff"><b>PlcOutput&lt;OutputData&gt;</b></font></td></tr>
      <tr><td colspan="2" bgcolor="#455a64"><font color="#cfd8dc">PLC &#8594; host</font></td></tr>
      <tr><td bgcolor="#d6e4f0"><b>sync</b></td><td bgcolor="#d6e4f0" align="left">txSeqNum &#160; rxAckNum</td></tr>
      <tr><td bgcolor="#fdebcf"><b>logs</b></td><td bgcolor="#fdebcf" align="left">writeIndex<br align="left"/>logBuffer[N]</td></tr>
      <tr><td bgcolor="#d7ecd9"><b>conf</b></td><td bgcolor="#d7ecd9" align="left">net / ntp / mqtt / geo</td></tr>
      <tr><td bgcolor="#eeeeee"><b>data</b></td><td bgcolor="#eeeeee" align="left"><font color="#666666">&lt;site payload&gt;</font></td></tr>
    </table>
  >];

  {rank=same; in; out;}
  in -> out [style=invis];
}
\enddot

Every struct is explicitly padded so its layout is identical packed or not, and
the cursors (`writeIndex` / `readIndex`) lead their structs so the host can poll
them through a word-aligned `uint32_t*` regardless of payload size. `static_assert`s
in `vars.h` pin the sizes and offsets; the OPTA sketch additionally asserts the
combined struct matches the PLC IDE's generated types byte-for-byte.

## Transactional handshake

Each direction is a sequence/acknowledgement pair. The sender bumps its `SeqNum`
after committing a payload; the receiver copies it into its `AckNum` once it has
the data. Equality means "in sync".

\dot "one direction's sequence/ack handshake across the two cores"
digraph handshake {
  stylesheet="graphs.css";
  bgcolor="transparent"; rankdir=TB; fontname="Helvetica"; nodesep=0.5; ranksep=0.45;
  node [shape=box, style="rounded,filled", fontname="Helvetica", fontsize=9, margin="0.12,0.06"];
  edge [arrowsize=0.7, fontname="Helvetica", fontsize=8];

  wlbl [shape=plaintext, style="none", fillcolor="none", label=<<b>writer</b><br/><font color="#666666" point-size="8">RX: PLC&#160;&#160;|&#160;&#160;TX: host</font>>];
  rlbl [shape=plaintext, style="none", fillcolor="none", label=<<b>reader</b><br/><font color="#666666" point-size="8">RX: host&#160;&#160;|&#160;&#160;TX: PLC</font>>];

  w1 [class="serial", label="write payload\nto shared memory"];
  w2 [class="serial", label="SeqNum += 1"];
  w3 [class="serial", label="sees SeqNum == AckNum\n(in sync)"];
  r1 [class="plc", label="sees SeqNum != AckNum"];
  r2 [class="plc", label="copy payload out\n(DSB acquire)"];
  r3 [class="plc", label="AckNum = SeqNum"];

  wlbl -> w1 [style=invis]; w1 -> w2 -> w3;
  rlbl -> r1 [style=invis]; r1 -> r2 -> r3;

  { rank=same; wlbl; rlbl; }
  { rank=same; w2; r1; }
  { rank=same; w3; r3; }
  w2:e -> r1:w [constraint=false, style=dashed, color="#1565c0", fontcolor="#1565c0", label="SeqNum"];
  r3:w -> w3:e [constraint=false, style=dashed, color="#2e7d32", fontcolor="#2e7d32", label="AckNum"];
}
\enddot

Payload copies go through `HAL::ordered_copy` (memcpy + DSB); the cross-core
counters (`SeqNum` / `AckNum`) are polled with `HAL::ordered_read` (volatile load
+ DMB), so a counter read can't be reordered against the payload copy it gates.
This is what makes the exchange safe against an unordered PLC runtime.

On the M7 the shared region may be cacheable, and we can't assume the proprietary
image toolchain maps it non-cacheable, so the handler also maintains the D-cache
at the `step()` boundaries (`HAL::cache_invalidate` / `cache_clean` - no-ops on
the simulator and on cores without a D-cache): it **invalidates** the PLC-output
region before reading it and **cleans** the PLC-input region after writing it.
Read and write regions are disjoint, and both structs are `alignas(32)` so a
cache line never straddles the two.

## Torn-read diagnostic

The handshake alone provides mutual exclusion - the PLC holds its payload steady
until it observes our ACK (ENABLE stays low until then) - so there is no seqlock
and no double buffer. As a cheap check that the contract holds, `RX_READ` reads
`txSeqNum` before the copy, copies, invalidates the sync line, then re-reads
`txSeqNum`: a mismatch means the PLC moved underneath us. It increments `_rx_torn`
(exposed via `rx_torn_count()`), logs a rate-limited `PLC_IO` warning, and returns
to `RX_IDLE` without acking. Diagnostic only - no retry loop.

## State machines

`PlcSyncHandler` (`plcio/sync.h`) runs two independent machines per `step()`. The
simulator's `PlcEmulator` (`plcio/emulator.h`) mirrors them from the PLC side.

\dot "the two independent RX and TX state machines, stepped once per beat"
digraph state_machines {
  stylesheet="graphs.css";
  bgcolor="transparent"; rankdir=TB; fontname="Helvetica";
  node [shape=box, style="rounded,filled", fontname="Helvetica", fontsize=9, margin="0.13,0.06"];
  edge [fontname="Helvetica", fontsize=8, arrowsize=0.7];

  subgraph cluster_rx {
    label="RX  -  read PLC output"; labeljust="l"; fontsize=10; style="rounded"; color="#b0bec5"; bgcolor="#fafafa";
    rx_idle [label="RX_IDLE", class="neutral"];
    rx_read [label="RX_READ", class="serial"];
    rx_ack  [label="RX_ACK",  class="plc"];
    rx_idle -> rx_read [label="txSeqNum != txAckNum"];
    rx_read -> rx_ack  [label="copy payload (DSB)"];
    rx_ack  -> rx_idle [label="txAckNum = txSeqNum"];
  }
  subgraph cluster_tx {
    label="TX  -  write PLC input"; labeljust="l"; fontsize=10; style="rounded"; color="#b0bec5"; bgcolor="#fafafa";
    tx_idle   [label="TX_IDLE",   class="neutral"];
    tx_write  [label="TX_WRITE",  class="serial"];
    tx_commit [label="TX_COMMIT", class="plc"];
    tx_wait   [label="TX_WAIT",   class="logic"];
    tx_idle   -> tx_write  [label="write requested\nor heartbeat"];
    tx_write  -> tx_commit [label="copy payload (DSB)"];
    tx_commit -> tx_wait   [label="rxSeqNum += 1"];
    tx_wait   -> tx_idle   [label="ack'd"];
    tx_wait   -> tx_commit [label="stalled a heartbeat", constraint=false, style=dashed];
  }
}
\enddot

The TX machine doubles as the link's keepalive: when idle it re-commits every
`PLC_HEARTBEAT_MS` (an unsolicited pulse), and a commit left unacknowledged for a
whole heartbeat in `TX_WAIT` is re-pulsed.

## Link status & timing

`PlcSyncContext::status()` derives the link state purely from how long it has been
since the last acknowledged TX:

\dot "link state by elapsed time since the last acknowledged TX (any ack returns to RUNNING)"
digraph link_status {
  stylesheet="graphs.css";
  bgcolor="transparent"; rankdir=TB; nodesep=0.4; ranksep=0.45;
  node [shape=box, style="rounded,filled", fontname="Helvetica", fontsize=10, margin="0.16,0.07"];
  edge [arrowsize=0.7, fontname="Helvetica", fontsize=9];

  unknown [label="PLC_UNKNOWN", class="neutral"];
  running [label="PLC_RUNNING", class="accent"];
  timeout [label="PLC_TIMEOUT", class="logic"];
  fault   [label="PLC_FAULT",   class="intercept"];

  unknown -> running [label="first ack"];
  running -> timeout [label="silent &#8805; PLC_TIMEOUT_MS"];
  timeout -> fault   [label="silent &#8805; PLC_WATCHDOG_MS"];
  timeout -> running [label="ack", style=dashed, color="#2e7d32", fontcolor="#2e7d32", constraint=false];
  fault   -> running [label="ack", style=dashed, color="#2e7d32", fontcolor="#2e7d32", constraint=false];
}
\enddot

| Status        | Condition                                   |
|---------------|---------------------------------------------|
| `PLC_UNKNOWN` | no TX ever acknowledged                     |
| `PLC_RUNNING` | acked within `PLC_TIMEOUT_MS`               |
| `PLC_TIMEOUT` | silent past `PLC_TIMEOUT_MS`                |
| `PLC_FAULT`   | silent past `PLC_WATCHDOG_MS`               |

Transitions are logged once (on change) to the `PLC_IO` topic. The PLC's own log
ring is drained separately by `PlcLogReader` (`plcio/logs.h`), which detects
dropped entries via sequence-number gaps and forwards the rest into the local
`Logger` (see @ref md_docs_2LOG__FLOW). Timing constants live in
`libbotanik_config.h` (`PLC_HEARTBEAT_MS`, `PLC_TIMEOUT_MS`, `PLC_WATCHDOG_MS`).

## Cross-core memory safety

The host (M7, with an L1 D-cache) and the PLC (M4, no D-cache) share this memory
with **no hardware coherency** and no assumption that the PLC uses ordered
accesses. Three layered guarantees keep it sound; see `plcio/sync.h`, `hal/`.

### Mutual exclusion: the handshake contract (not double buffering)

There is **one** shared `data` buffer per direction, not a double buffer. Torn
reads are prevented by the seqnum/ack ping-pong, which the PLC side must honour:

- The PLC writes the payload, then bumps `txSeqNum`. The host sees
  `txSeqNum != txAckNum`, copies the payload into a *local* buffer, then sets
  `txAckNum = txSeqNum`.
- **Contract:** the PLC must not touch the payload again until it observes the
  host's ack. In the PLC program this is a `MOV` block with its `ENABLE` input
  held low until the ack is seen, then raised for the next update.

So the host always works on a private snapshot, and the buffer is never written
while it is being read - *as long as the PLC upholds the contract*.

### Torn-read diagnostic (defence in depth)

Because the contract lives on the PLC side, the host verifies it cheaply: after
copying the payload, `_rx_memcpy()` re-reads `txSeqNum`; if it changed during the
copy, the snapshot may be torn, so the host **discards** it, counts it
(`rx_torn_count()`), logs a `PLC_IO` warning, and **does not ack** (it re-reads next
tick). This is a diagnostic, not a retry loop - it surfaces a contract violation
instead of silently consuming bad data.

### Memory ordering and cache coherency

- **Compiler ordering / visibility:** the cross-core sync counters are polled via
  `HAL::ordered_read` (a `volatile` load + `dmb()`), so the compiler can't cache a
  stale value and the read is ordered. Payloads are moved with `HAL::ordered_copy`
  (word-granular on the device, `dsb()`-fenced).
- **D-cache coherency (M7 only):** the shared regions are `alignas(32)` so each
  owns its cache lines. The host **invalidates** the PLC-output region before
  reading it and **cleans** the PLC-input region after writing it
  (`HAL::cache_invalidate`/`cache_clean`, no-ops where there is no D-cache). Read
  and write regions are disjoint, so a clean never races an invalidate. If the
  shared region turns out to be mapped non-cacheable, these are cheap no-effect
  ops; if cacheable, they are required for the M4 to see the host's writes.
