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
| `Pin` | the label set | the terminals a site's tables name: `IN1`..`IN8`, `BUTTON`, `RELAY_1`..`RELAY_4`, `LED_1`..`LED_4`, `LED_BLUE`. ONE flat space, and the backend maps each to whatever the board calls it. The values are OURS - `Arduino.h` reaches exactly one TU because it `#define`s `Serial`, so a table compiled for both targets cannot say the core's `RELAY1` and mean it. Four labels are spelled apart from the core's macros for that reason (`BUTTON`, `RELAY_n`, `LED_n`, `LED_BLUE`) |


**Serial is virtual on the OPTA, and deliberately so.** `CONSOLE` and `MONITOR`
are logical endpoints, each behind one small interface, so moving a role to a
different physical port is a one-line change in `get()`. Virtual dispatch is
cheap there: two long-lived RAM objects, not a flash-resident descriptor table
where a vptr would force the table out of `.rodata`.| `DigitalOut` | `attach`, `write`, `read`, `owns` | the on-board relays and status LEDs, which are one thing to the firmware: a pin it drives. `read` answers from a shadow on both backends, since a coil is not readable |
| `AnalogIn` | `attach`, `read`, `inject`, `owns` | the SAME eight terminals read as voltages: on the OPTA they are 0-10 V capable, so whether a terminal is a contact or a sensor is the ROW's declaration and nothing else. `read` answers a raw count at `BITS`; engineering units are the row's calibration, applied by `Bank::publish_wire()` - the same path a Modbus analog row takes |
| `DigitalIn` | `attach`, `read`, `inject`, `owns` | the controller's OWN input terminals, 8 of them, read as levels. `inject` is the simulator's way in and a no-op on hardware, where the field drives the wire |
| `Pulse` | `attach`, `sample`, `overruns`, `inject` | the same terminals COUNTED instead: a meter's pulse line, where the level of the wire is not a reading of anything. `attach` is idempotent for the same edge, because bring-up runs twice in one process every time a test builds a second firmware. `sample` returns the count and the instant TOGETHER, re-read until stable, or an edge landing between them pairs a new count with an old clock |
| `Env` | `u32`, `u8`, `str` | environment overrides; may return the default everywhere |
| `Debug` | `breakpoint`, `under_debugger` | `under_debugger` gates `breakpoint`, so a bare trap never faults a debugger-less unit |
| `System` | `reset`, `shutdown` | |
| *(top level)* | `hardware_description` | one line, shown by `version` |

@note **There is no `AnalogOut`.** The OPTA has eight inputs, four relays,
four LEDs and a button, and no analog output terminal: the variant's
`#define DAC (A6)` is inherited boilerplate, and `A6` is terminal `I7` - so a
DAC there would drive a pin behind the input conditioning, one a site may
already be reading. Analog out comes from an expansion module, which is a
field-bus address rather than a local pin, and `scripts/tables.py plant`
refuses an `AO` row on device 0 for that reason. A board that really has one
adds the namespace and the labels together.

@note **`attach()` is where a local pin is CONFIGURED**, and it is the only
place: `pinMode`, the pull, the converter's resolution. Call it from setup(),
once per pin, and treat a refusal as a wiring fault rather than retrying - the
four namespaces share eight terminals, and a pin configured two ways is a
hardware fault waiting for the first wet week. Field-bus modules are NOT
configured here: an RS-485 module is addressed and set up by hand at
commissioning, and the firmware only reads and writes what it was told.

@note **`Pulse::attach()` may refuse a channel your hardware could serve.** On the
OPTA, terminals I1 and I4 share an EXTI line and at most one of the pair may carry
an interrupt. The SIMULATOR enforces the same rule although nothing on a host shares
a line - otherwise a site that only ever runs in the simulator would discover it on
the bench. A port to different hardware keeps the refusal shape (`false` from
`attach`) even where its own collisions are elsewhere, or none.

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
