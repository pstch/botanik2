# Porting to other hardware {#dev_porting}

Everything platform-specific lives behind one seam: the HAL
(`src/libbotanik/hal/`). The rest of the codebase calls only `HAL::` functions and
never touches `<time.h>`, Mbed, or board APIs. So a new target is structurally one
new HAL implementation - no subsystem changes.

## The seam

`hal/platform.h` declares the interface and selects an implementation at compile
time:

```cpp
#ifdef TARGET_OPTA
    #include "libbotanik/hal/opta.h"
#else
    #include "libbotanik/hal/simulator.h"
#endif
```

A new target adds `hal/<target>.h` implementing every declared function, a
`TARGET_<NAME>` define, a branch here, and a CMake toolchain entry. `simulator.h` and
`opta.h` are the worked references: the simulator gives the "no real hardware"
answers (barriers as no-ops, time from `std::chrono`), the OPTA the device answers.

@warning `TARGET_OPTA` must appear **only** under `hal/`. Shared code never branches
on the target. `TARGET_SIMULATOR` may, but only for development-only scaffolding.

## What to implement

The interface is grouped into nested namespaces by concern.

| Namespace | Functions | Notes |
|---|---|---|
| `Clock` | `current_ms`, `uptime_ms`, `wall_ms`, `sleep_ms` | a monotonic millisecond clock is the only hard requirement; wall clock may be a stub |
| `Clock` | `hrt_ticks`, `hrt_us` | the INSTRUMENTATION clock, separate because the beat's clock need not resolve microseconds and every per-task figure is one. A free-running counter that wraps, plus its conversion; may fall back to `uptime_us` |
| `Memory` | `dsb`, `dmb`, `ordered_read`, `ordered_copy`, `ordered_move` | real barriers on a target with concurrent publishers; a plain fenced `memcpy`/store where there is one core and no cache. See @ref dev_concurrency |
| `Memory` (cache) | `cache_clean`, `cache_invalidate` | D-cache maintenance by range. Cheap no-ops where there is no D-cache - the simulator implements both as empty |
| `Stack` | `high_water` | feeds the scheduler's stack-usage metric. ONE call answering `{used, depth}` together: the two used to come from different stacks on the OPTA. `used` 0 means not measurable |
| `Watchdog` | `start`, `kick`, `stop` | the hardware watchdog the health monitor gates |
| `Serial` | `Raw`, `Port`, `get` | the console and monitor ports |
| `Storage` | `set`, `get`, `remove` | key-value persistence. Mbed KVStore on OPTA, one file per key on the simulator |
| `Network` | link, DNS, UDP, TCP (`hal/net.{h,cpp}`) | only needed under `FEATURE_NETWORK` |
| `Env` | `u32`, `u8` | environment overrides; may return the default everywhere |
| `Debug` | `breakpoint`, `under_debugger` | `under_debugger` gates `breakpoint`, so a bare trap never faults a debugger-less unit |
| `System` | `reset`, `shutdown` | |
| *(top level)* | `hardware_description` | one line, shown by `version` |

## Validating a port

The simulator and the OPTA build already exercise the two ends of the spectrum; a new
HAL is correct when the existing subsystems run unchanged on top of it. Bring it up
against the @ref reference "reference site" and work through:

1. **The boot completes and POST passes** - `POST ok (site <name>)` on the `HEALTH`
   topic. POST only checks structural wiring (@ref dev_health), so a pass means the
   logger reached a sink, which means your `Serial` works.
2. **`stats` reports sane figures.** A CPU load of 0 % or 100 %, or a stack usage of
   0 %, means `Clock` or `Stack` is stubbed rather than implemented.
3. **The watchdog actually bites.** Withhold a kick deliberately and confirm the unit
   resets. A watchdog that never fires is worse than none.
4. **Settings survive a reboot** - `set` something, reboot, `get` it back. A
   `FLASH_FLT` alarm, or a `params save all` that leaves rows still marked unsaved, points at `Storage`.
5. **The pyte screen suite passes**, if the target can be driven over a PTY - it
   asserts the rendered screen byte for byte and will catch a terminal or timing
   difference nothing else notices.
