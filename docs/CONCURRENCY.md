# Concurrency model

This firmware (the "C side", running on the OPTA's **M7** core) is deliberately
single-threaded with a small, well-defined set of asynchronous boundaries. Keeping
that surface tiny is what lets the codebase use plain `volatile` + memory barriers
instead of `std::atomic` or locks.

## One cooperative loop

All of our code runs in a single fixed-period main loop (`LoopScheduler`, period
`Config::TASK_PERIOD_MS`). Every subsystem exposes a non-blocking `step()` and is
called once per beat in a fixed order - the **per-tick cycle contract**:

```
tick -> receive -> plc_handler -> plc_log_reader -> health -> on_loop
     -> logger -> terminal -> monitor.drain -> transmit -> watchdog_kick
```

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

## The discipline (no `std::atomic`)

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