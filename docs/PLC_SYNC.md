# PLC Shared-Memory Sync

How the host (the M7 core running this firmware) exchanges data with the PLC
coprocessor (the M4 core) over a shared-memory region, without assuming the PLC
runtime uses ordered memory accesses.

## Shared memory layout

Two regions, each a `PlcInput<Data>` / `PlcOutput<Data>` (`plcio/vars.h`). "Input"
and "Output" are named from the **PLC's** point of view: the host *writes* the
input region and *reads* the output region.

Each region holds the same three sections (sync cursors, the log ring, the site
payload); `PlcOutput` adds the `conf` config block (see @ref md_docs_2PLC__CONFIG):

\startuml "the two shared regions and their sections"
class "PlcInput<InputData>\n(host -> PLC)" as PI {
  sync : rxSeqNum, txAckNum
  logs : readIndex
  data : site payload
}
class "PlcOutput<OutputData>\n(PLC -> host)" as PO {
  sync : txSeqNum, rxAckNum
  logs : writeIndex, logBuffer[N]
  conf : net / ntp / mqtt / geo
  data : site payload
}
\enduml

Each section is its own named struct (`plcio/vars.h`), named from the **PLC's** point of
view. The `sync` counters are one word per direction pair; the `logs` section is the
host's read cursor on the input side and the PLC's write cursor plus the ring of
`LogEntry_t` records on the output side:

\startuml "the sync-cursor and log-ring section structs (PLC's point of view)"
class PlcInputCounters_t {
  rxSeqNum : uint32 - PLC's copy of the host TX seq
  txAckNum : uint32 - host's ack of the PLC TX seq
}
class PlcOutputCounters_t {
  txSeqNum : uint32 - PLC TX seq (bumped on commit)
  rxAckNum : uint32 - PLC's ack of the host TX seq
}
class PlcInputLogs_t {
  readIndex : uint32 - host read cursor (leads the struct)
}
class PlcOutputLogs_t {
  writeIndex : uint32 - PLC write cursor (leads the struct)
  logBuffer : LogEntry_t[N] - the log ring
}
PlcOutputLogs_t "1" *-- "N" LogEntry_t
\enduml

Every struct is explicitly padded so its layout is identical packed or not, and the
cursors (`writeIndex` / `readIndex`) lead their structs so the host can poll them
through a word-aligned `uint32_t*` regardless of payload size. `static_assert`s in
`vars.h` pin sizes and offsets; the OPTA sketch additionally asserts the combined
struct matches the PLC IDE's generated types byte-for-byte.

## Transactional handshake

Each direction is a sequence/acknowledgement pair. The sender bumps its `SeqNum`
after committing a payload; the receiver copies it into its `AckNum` once it has
the data. Equality means "in sync".

\startuml "one direction's sequence/ack handshake across the two cores"
hide footbox
participant writer
participant reader
writer -> writer : write payload to shared memory
writer ->> reader : SeqNum += 1
reader -> reader : sees SeqNum != AckNum
reader -> reader : copy payload out (DSB acquire)
reader -->> writer : AckNum = SeqNum
writer -> writer : sees SeqNum == AckNum (in sync)
\enduml

The memory ordering (`HAL::ordered_copy` / `ordered_read`), the M7 D-cache
maintenance, and the cheap torn-read check that back this handshake are detailed
under *Cross-core memory safety* below.

## State machines

`PlcSyncHandler` (`plcio/sync.h`) runs two independent machines per `step()`. The
simulator's `PlcEmulator` (`plcio/emulator.h`) mirrors them from the PLC side.

\startuml "the two independent RX and TX state machines, stepped once per beat"
state "RX - read PLC output" as RX {
  [*] --> RX_IDLE
  RX_IDLE --> RX_READ : txSeqNum != txAckNum
  RX_READ --> RX_ACK : copy payload (DSB)
  RX_ACK --> RX_IDLE : txAckNum = txSeqNum
}
state "TX - write PLC input" as TX {
  [*] --> TX_IDLE
  TX_IDLE --> TX_WRITE : write requested or heartbeat
  TX_WRITE --> TX_COMMIT : copy payload (DSB)
  TX_COMMIT --> TX_WAIT : rxSeqNum += 1
  TX_WAIT --> TX_IDLE : ack'd
  TX_WAIT --> TX_COMMIT : stalled a heartbeat
}
\enduml

The TX machine doubles as the link's keepalive: when idle it re-commits every
`PLC_HEARTBEAT_MS` (an unsolicited pulse), and a commit left unacknowledged in
`TX_WAIT` for a whole heartbeat is re-pulsed.

## Link status & timing

`PlcSyncContext::status()` derives the link state purely from how long it has been
since the last acknowledged TX:

\startuml "link state by elapsed time since the last acknowledged TX (any ack returns to RUNNING)"
[*] --> PLC_UNKNOWN
PLC_UNKNOWN --> PLC_RUNNING : first ack
PLC_RUNNING --> PLC_TIMEOUT : silent >= PLC_TIMEOUT_MS
PLC_TIMEOUT --> PLC_FAULT : silent >= PLC_WATCHDOG_MS
PLC_TIMEOUT --> PLC_RUNNING : ack
PLC_FAULT --> PLC_RUNNING : ack
\enduml

| Status        | Condition                                   |
|---------------|---------------------------------------------|
| `PLC_UNKNOWN` | no TX ever acknowledged                     |
| `PLC_RUNNING` | acked within `PLC_TIMEOUT_MS`               |
| `PLC_TIMEOUT` | silent past `PLC_TIMEOUT_MS`                |
| `PLC_FAULT`   | silent past `PLC_WATCHDOG_MS`               |

Transitions are logged once (on change) to the `PLC_IO` topic. The PLC's own log ring
is drained separately by `PlcLogReader` (`plcio/logs.h`), which detects dropped
entries via sequence-number gaps and forwards the rest into the local `Logger` (see
@ref md_docs_2LOG__FLOW). Timing constants are in `libbotanik_config.h`.

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
(`rx_torn_count()`), logs a `PLC_IO` warning, and **does not ack** (re-reading next
tick). It is a diagnostic, not a retry loop: it surfaces a contract violation instead
of silently consuming bad data.

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
