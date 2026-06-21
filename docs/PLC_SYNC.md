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

```
   sender                              receiver
   ──────                              ────────
   write payload to shared memory
   SeqNum += 1            ───────────▶ sees SeqNum != AckNum
                                       copy payload out (DSB acquire)
   sees SeqNum == AckNum  ◀─────────── AckNum = SeqNum
```

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

```
 RX (read PLC output)            TX (write PLC input)
 ┌─────────┐                     ┌─────────┐  write requested
 │ RX_IDLE │◀──┐                 │ TX_IDLE │◀──┐ or heartbeat elapsed
 └────┬────┘   │                 └─────┬───┘   │
 SeqNum≠Ack    │                  write│ │heartbeat
      ▼        │                       ▼ ▼
 ┌─────────┐   │                 ┌──────────┐
 │ RX_READ │   │ack              │ TX_WRITE │
 └────┬────┘   │                 └─────┬────┘
   copy        │                   copy│
      ▼        │                       ▼
 ┌─────────┐   │                 ┌──────────┐
 │ RX_ACK  │───┘                 │ TX_COMMIT│ SeqNum += 1
 └─────────┘                     └─────┬────┘
                                      ▼
                                 ┌──────────┐ ack'd → TX_IDLE
                                 │ TX_WAIT  │ stalled a heartbeat → re-commit
                                 └──────────┘
```

The TX machine doubles as the link's keepalive: when idle it re-commits every
`PLC_HEARTBEAT_MS` (an unsolicited pulse), and a commit left unacknowledged for a
whole heartbeat in `TX_WAIT` is re-pulsed.

## Link status & timing

`PlcSyncContext::status()` derives the link state purely from how long it has been
since the last acknowledged TX:

\dot "link state by elapsed time since the last acknowledged TX"
digraph link_status {
    bgcolor="transparent";
    rankdir=LR;
    node [shape=box, style=rounded, fontname="Courier", fontsize=10, margin="0.08,0.04"];
    edge [arrowsize=0.7, fontname="Helvetica", fontsize=9];

    PLC_RUNNING [style="rounded,filled", fillcolor="#e6f4ea"];
    PLC_TIMEOUT [style="rounded,filled", fillcolor="#fef7e0"];
    PLC_FAULT   [style="rounded,filled", fillcolor="#fce8e6"];

    PLC_RUNNING -> PLC_TIMEOUT [label="≥ PLC_TIMEOUT_MS"];
    PLC_TIMEOUT -> PLC_FAULT   [label="≥ PLC_WATCHDOG_MS"];
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
