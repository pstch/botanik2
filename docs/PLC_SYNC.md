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
 ┌────────────────────────┐     ┌────────────────────────┐
 │ sync   rxSeqNum txAckNum│     │ sync   txSeqNum rxAckNum│
 ├────────────────────────┤     ├────────────────────────┤
 │ logs   readIndex        │     │ logs   writeIndex       │
 │                         │     │        logBuffer[N]     │
 ├────────────────────────┤     ├────────────────────────┤
 │ data   <site payload>   │     │ data   <site payload>   │
 └────────────────────────┘     └────────────────────────┘
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

All payload copies go through `HAL::ordered_copy` (memcpy + DSB) and the RX read
issues a `HAL::dsb()` *acquire* before copying, so the payload reads can't be
reordered ahead of the `SeqNum` check that gated them. This is what makes the
exchange safe against an unordered PLC runtime — see @ref md_docs_2EVENT__DISPATCH
for the unrelated terminal path.

## State machines

`PlcSyncHandler` (`plcio/sync.h`) runs two independent machines per `step()`. The
simulator's `PlcEmulator` (`plcio/emulator.h`) mirrors them from the PLC side.

```
 RX (read PLC output)            TX (write PLC input)
 ┌─────────┐                     ┌─────────┐  write requested
 │ RX_IDLE │◀──┐                 │ TX_IDLE │◀──┐ or heartbeat elapsed
 └────┬────┘   │                 └────┬────┘   │
 SeqNum≠Ack    │                  write│ │heartbeat
      ▼        │                       ▼ ▼
 ┌─────────┐   │                 ┌──────────┐
 │ RX_READ │   │ack              │ TX_WRITE │
 └────┬────┘   │                 └────┬─────┘
   copy        │                   copy│
      ▼        │                       ▼
 ┌─────────┐   │                 ┌──────────┐
 │ RX_ACK  │───┘                 │ TX_COMMIT│ SeqNum += 1
 └─────────┘                     └────┬─────┘
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

```
 last ack ──────────────────────────────────────────────▶ time since
 │            │ PLC_TIMEOUT_MS        │ PLC_WATCHDOG_MS
 │ PLC_RUNNING│ PLC_TIMEOUT           │ PLC_FAULT
```

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
