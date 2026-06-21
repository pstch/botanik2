# PLC Shared-Memory Sync

How the host (the M7 core running this firmware) exchanges data with the PLC
coprocessor (the M4 core) over a shared-memory region, without assuming the PLC
runtime uses ordered memory accesses.

## Shared memory layout

Two regions, each a `PlcInput<Data>` / `PlcOutput<Data>` (`plcio/vars.h`). "Input"
and "Output" are named from the **PLC's** point of view: the host *writes* the
input region and *reads* the output region.

```
 PlcInput<InputData>            PlcOutput<OutputData>
 (host → PLC)                   (PLC → host)
 ┌─────────────────────────┐     ┌─────────────────────────┐
 │ sync   rxSeqNum txAckNum│     │ sync   txSeqNum rxAckNum│
 ├─────────────────────────┤     ├─────────────────────────┤
 │ logs   readIndex        │     │ logs   writeIndex       │
 │                         │     │        logBuffer[N]     │
 ├─────────────────────────┤     ├─────────────────────────┤
 │ data   <site payload>   │     │ data   <site payload>   │
 └─────────────────────────┘     └─────────────────────────┘
```

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

### Confirmed design decisions (round-2 review)

These were examined and deliberately left as-is; the rationale is recorded so a
future change doesn't "fix" a non-bug:

- **First contact uses a bind-time ack baseline.** The handler snapshots the PLC's
  seq/ack at construction and seeds our input region to a known start (rxSeqNum 0;
  txAckNum = the PLC's current txSeqNum). The first ack is accepted only once
  `ack != _ack_baseline`, so a *stale* rxAckNum left in shared memory can't declare
  the link RUNNING (and feed the watchdog) before the PLC has actually replied.
- **32-bit seqnums, not 64-bit.** The seqnum gap math is unsigned-modular and so is
  already wrap-correct; first contact is handled by a `_have_seq` flag, not a zero
  sentinel. 64-bit counters were rejected: they would force an 8-byte-aligned, larger
  `LogEntry_t` the PLC must mirror - a wire-ABI change for no correctness gain. If
  absolute log identity is ever needed (e.g. MQTT log export), settle it as one
  coordinated PLC-contract change alongside a 64-bit timestamp.
- **TX has no torn-read diagnostic, by design.** Only RX carries `rx_torn_count()`.
  The handshake already makes a torn TX commit impossible to *act on*; if TX
  torn-*detection* is ever wanted, the fit is a seqlock over the single region, NOT
  double-buffering (which the handshake deliberately avoids - see above).
- **The log reader consumes a local snapshot of the write index.** A writeIndex that
  advances mid-read is not a torn read: the reader just picks up the new entries on
  the next `step()`. Delivery is intentionally batched per beat, bounded by the ring.
- **The simulator's barriers/cache ops are conservative no-ops.** The sim has no
  D-cache and one address space; `dsb()`/`cache_*` there only mirror the device code
  path so the two targets stay structurally identical. The device ordering is the
  one that carries correctness.
