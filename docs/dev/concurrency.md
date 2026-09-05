# Concurrency model {#dev_concurrency}

The firmware is deliberately single-threaded with a very small set of asynchronous
boundaries. Keeping that surface tiny is what lets the codebase use plain `volatile`
plus memory barriers instead of `std::atomic` or locks.

## One cooperative loop

Everything runs in one fixed-period main loop. Every subsystem exposes a
non-blocking `step()` and is called once per beat in a fixed order - the per-tick
cycle contract, documented in @ref dev_architecture. The order lives in exactly one
place, `BotanikFirmware::loop()`, so it cannot drift between targets.

**There are no serial ISRs in our code.** `BufferedSerial::receive()` /
`transmit()` / `step()` all run on the main loop; the platform UART driver keeps its
own interrupt handling *below* the HAL. The `BufferedSerial` ring and the
`BufferedLogWriter` queue are therefore **not ISR-safe** and must only be touched
from the main loop, as their headers say.

## The async boundaries

There are two, and only two.

**1. The asynchronous DNS resolver.** On the OPTA, `gethostbyname_async()` completes
on the network stack's own thread and publishes into a slot the main loop polls:

```cpp
struct DnsSlot { volatile uint8_t st; Ip4 addr; ... };  // 0 free, 1 pending, 2 ok, 3 failed

void on_dns(DnsSlot* slot, int result, SocketAddress* a) {
    if (result > 0 && a != 0) {
        memcpy(slot->addr.b, a->get_addr().bytes, 4);
        HAL::Memory::dsb(); slot->st = 2;      // payload before the flag
    } else { HAL::Memory::dsb(); slot->st = 3; }
}
```

The discipline is the whole contract: the payload is written first, a `dsb()`
separates it from the state store, and the state is `volatile` so the poll cannot be
hoisted. The reader never touches `addr` unless `st` says it is published.

**2. The hardware watchdog.** Fire-and-forget: a timer that resets the MCU unless
`Watchdog::kick()` runs each healthy beat. It shares no mutable state beyond the kick
itself, so it needs no synchronisation. On the simulator it is serviced from its own
thread, deliberately independent of the main loop, so a wedged loop still trips it.

That is the entire surface. Field I/O is Modbus over the loop, storage is
synchronous, and there is no coprocessor.

## The discipline (no `std::atomic`)

Because the async surface is that small, `hal/platform.h` exposes a handful of
primitives and nothing else is needed:

| | |
|---|---|
| `HAL::Memory::dsb()` | full barrier: all preceding explicit accesses complete |
| `HAL::Memory::dmb()` | lighter: orders explicit accesses without waiting for completion |
| `HAL::Memory::ordered_read<T>()` | `volatile` load (never cached in a register) plus `dmb()` for acquire ordering |
| `HAL::Memory::ordered_copy<T>()` | fenced block publish: `T` must be a word multiple, and source and destination must not alias |
| `HAL::Memory::ordered_move<T>()` | the scalar counterpart: store, then fence |
| `HAL::Memory::cache_clean()` | write back the D-cache over a range, so another core sees it in SRAM |
| `HAL::Memory::cache_invalidate()` | drop cached lines over a range, so a stale copy cannot hide another core's writes |

@note The last four currently have **no callers**. They are kept deliberately, for
publishing state to the M4 core when work starts moving there - this is the kind of
primitive that wants to be right rather than written in a hurry. Do not remove them
in a dead-code sweep; `tests/test_hal_memory.cpp` exists to keep them compiled and
type-checked in the meantime.

The two cache calls are not symmetric. `cache_clean` only ever writes back, so
rounding up to a whole line loses nothing and it is always safe. `cache_invalidate`
**discards** whole lines, so it is safe only on a region we never write - invalidate
what the other core produces, clean what we produce, and align the two regions to 32
bytes so they cannot share a line. Invalidating a line holding one of our own writes
drops that write silently.

@warning Do not reach for `std::atomic`, and do not add a mutex. If a change seems to
need one, the real problem is almost certainly that something ended up on the wrong
side of a boundary - fix that instead.

## One time base

`SchedulerContext::timestamp_ms` is **64-bit** and does not wrap. `tick()`
publishes it once per beat as the GRID point rather than the wall reading, and
everything stepped in that beat shares it: the render path, alarm dwell, fault
grace and task scheduling alike. `HAL::Clock::uptime_ms()` is the same clock
read fresh, for anything outside a beat.

There is no wrap-safe subtraction discipline to keep any more, and no
`Utils::Millis32`: both went with the 32-bit field. The one place a wrapping
counter survives is `HAL::Clock::hrt_ticks()`, whose DELTAS are always taken in
unsigned arithmetic so the wrap needs no special case.

@warning `timestamp_ms` is the grid point, so it is up to a beat period behind
the wall clock. Anything measuring how late the beat itself was reads
`uptime_us()` directly, as `_record_jitter()` does.
