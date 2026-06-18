# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this 
repository.

## Project overview

Botanik is a greenhouse control system for an Arduino OPTA PLC (STM32H747 dual-core ARM). It manages irrigation, ventilation, cooling, and monitoring. The core is `libbotanik`, a C++ library designed to work around Arduino OPTA platform limitations. It builds either as a Linux simulator (`SIMULATOR` target) or for the actual hardware (`OPTA` target).

## Build

Requires CMake 3.10+ and a C++11-capable compiler. The default target is `SIMULATOR`.

```bash
# Configure (first time or after CMakeLists changes)
cmake -B build -DBOTANIK_TARGET=SIMULATOR

# Build
make -C build

# Build specific target
make -C build botanik_sim

# Cross-compile for OPTA hardware
cmake -B build -DBOTANIK_TARGET=OPTA
make -C build
```

The repo ships a `.envrc` that exports `MAKEFLAGS=-j4`, so `make` builds in
parallel by default once [direnv](https://direnv.net) loads it (`direnv allow`).
Without direnv, either `export MAKEFLAGS=-j4` in your shell or pass `-j` per build
(`make -C build -j$(nproc)`); a bare `make` is otherwise single-threaded.

`ccache` is used automatically as a compiler cache when it's installed (a no-op
otherwise), so a clean rebuild after `rm -rf build` replays cached objects in a
couple of seconds instead of recompiling.

OPTA builds use `arm-none-eabi-gcc/g++` and the Arduino Mbed OPTA core, which
arduino-cli installs under `~/.arduino15`:

```bash
arduino-cli core install arduino:mbed_opta
```

The CMake OPTA build auto-discovers the highest installed version there; override
with `-DMBED_CORE_ROOT=/path/to/packages/arduino/hardware/mbed_opta/<version>` or
the `ARDUINO_DATA_DIR` env var. (The build no longer uses the vendored `includes/`
tree.)

There are no tests yet.

## Architecture

The system is split into six subsystems under `src/libbotanik/`:

**HAL (`hal/`)** - `platform.h` defines the unified interface (`current_ms()`, `uptime_ms()`, `dsb()`, `ordered_copy/move`). `simulator.h` implements it with `std::chrono` and Linux serial I/O; `opta.h` implements it for Mbed OS. All other code uses HAL exclusively - never raw `<time.h>` or platform-specific calls.

**Terminal UI (`terminal/`)** - `AnsiTerminal` owns the serial connection and drives a multiplexed page display. `InputParser` decodes raw byte streams into `InputEvent`s (handles X3.64 CSI, Alt, and function-key escape sequences). `InputEditor` is a line-editing state machine that consumes `InputEvent`s. `Ansi` is a stateless helper that emits ANSI escape codes.

**Pages (`pages/`)** - Each page (`Home`, `Shell`, `Logs`, `Status`) implements the `Page` interface. `AnsiTerminal` routes input events to the active page and calls each page's render method when the display needs updating.

**Logging (`logs/`)** - `Logger` uses a dual-bucket token-bucket rate limiter (one global bucket, one per topic) before forwarding to registered `LogWriter`s. `LOG_CRITICAL` entries are undroppable (they evict the oldest queued entry instead of being dropped), and silent-loss categories (sink drops, TX-full truncation, rate-limiter suppressions) are reported edge-triggered as criticals. Writers include `BufferedLogWriter` and `SerialLogWriter`. Log topics are the `LogTopic` enum in `plcio/vars.h` (`PLC_IO`, `BOOT`, `NETWORK`, `TIME`, `STATUS`, and the firmware-only `ASSERT`).

**PLC I/O (`plcio/`)** - `PlcSyncHandler` manages two independent state machines (RX and TX) for shared-memory communication with the PLC coprocessor. Uses explicit ARM barriers (`HAL::dsb()`, plus `HAL::ordered_read()` for cross-core counters), D-cache maintenance at the `step()` boundaries (`HAL::cache_invalidate`/`cache_clean`, no-ops on the simulator), and transactional read/write patterns to prevent torn operations; a torn-read diagnostic (`rx_torn_count()`) flags any handshake violation. Timing constants: heartbeat 1 s, timeout 5 s, watchdog 30 s.

**Utils (`utils/`)** - `BufferedSerial` wraps UART with a 256-byte RX ring buffer and 4096-byte TX buffer. `time.h` provides uptime tracking on top of HAL.


### Key compile-time defines

| Define | Set when |
|---|---|
| `TARGET_SIMULATOR` | SIMULATOR build |
| `DEBUG` | SIMULATOR build |
| `_GNU_SOURCE` | SIMULATOR build |
| `TARGET_OPTA` | OPTA build |
| `ARDUINO` / `MBED_*` | OPTA build |

## Site-specific code (greenhouse targets)

`libbotanik` is generic and site-independent. Code specific to one physical greenhouse and its PLC program (the shared-memory payload layout, its pages, its alert table, and the mock data that animates it) is a **site**, code-named per greenhouse. The reference site is `gh_mlb41`.

Three layers, generic to specific:

| Layer | Holds | Examples |
|---|---|---|
| `libbotanik` | generic, site- and target-independent machinery, plus the `BotanikFirmware<In, Out>` base that owns the generic components, exposes a reference to each on the instance, and drives the per-tick `loop()` | `PlcSyncHandler`, `AnsiTerminal`, `AlertModel`, `BotanikFirmware` |
| site (`gh_mlb41`) | single source of truth for one greenhouse: the `PlcInput/OutputData_t` layout, site pages (Home/Status), the `g_alerts[]` table, the `fill_output()` data simulation, and a `GreenhouseMLB41` class deriving from `BotanikFirmware` | `gh_mlb41` |
| target / entry point | thin: selects one site and runs it | `botanik_sim` harness, generated OPTA `.ino` |

**Composition.** Components are split by layer and chained by inheritance (not an ideal composition model, but a structured start):

- `BotanikFirmware<In, Out>` constructs the generic components (logger, scheduler, terminal, PLC handler, log reader, alert reader, alert model, core pages) and exposes a reference to each on the instance.
- `GreenhouseMLB41 : BotanikFirmware<In, Out>` declares its site components (site pages, the alert table, the shell-command table, any site logic) as members, built from the base's exposed component references.
- A site component is often a static site-defined table plus an opaque ctx, registered on a base-owned generic component: the `g_alerts[]` table feeds `AlertModel`, a `ShellCommand[]` table registers on the shell page via `set_commands()`.
- Lifecycle chains up the hierarchy: `GreenhouseMLB41::setup()` calls `BotanikFirmware::setup()` first, then wires its own; same for `loop()`.
- The Arduino sketch may add one more slim subclass for last-mile customization, chaining `setup()` / `loop()` the same way.

Rules:

- **One data layout per site, shared by simulator and hardware.** A site's `PlcOutputData_t` / `PlcInputData_t` is used verbatim by both the OPTA firmware and the simulator's mock PLC. Never fork the layout per target; evolve the layout and its `fill_output()` animator together, in the site.
- **The simulator is a generic harness.** It allocates the shared region locally and drives a `PlcEmulator` with the selected site's `fill_output()`. On OPTA the real coprocessor plays that role and the region comes from `HAL::get_plc_input()/get_plc_output()`; the site code is identical either way.
- **The site stays portable.** Site code must cross-compile as an Arduino library: the deploy step zips the site dir and generates a `.ino` that statically allocates a `GreenhouseMLB41` and calls its `setup()` / `loop()`. The zip build does not define `TARGET_SIMULATOR`, so simulator-only code (the `fill_output()` animator, the emulator thread) drops out through its `#ifdef TARGET_SIMULATOR` guard. Keep such code guarded rather than in the portable path.
- **Generic components constrain the site payload.** Templated generics require fields on the site `PlcOutputData_t` (e.g. `AlertReader` / `StatusPage` need a 64-bit `alerts` bitset). The site defines its layout to satisfy those contracts.
- New site-level work goes in the site, not in `libbotanik` and not in a target's `main`.

**Current state (migration pending):** the `gh_mlb41` site is not yet extracted. Its definition still lives inside `botanik_sim/` (`simulator.h`/`.cpp`, `main.cpp`), and `botanik_fw/firmware.h` carries a diverging placeholder payload. Until the unification lands, treat the `botanik_sim` payload / pages / alert table / shell commands as the canonical `gh_mlb41` definition, and `BotanikFirmware` as the base to be promoted into `libbotanik`.
