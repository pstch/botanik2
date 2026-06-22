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

**HAL (`hal/`)** - `platform.h` defines the unified interface (`current_ms()`, `uptime_ms()`, `dsb()`, `ordered_copy/move`). `simulator.h` implements it with `std::chrono` and Linux serial I/O; `opta.h` implements it for Mbed OS. All other code uses HAL exclusively - never raw `<time.h>` or platform-specific calls. The HAL also owns the network transport (`hal/net.{h,cpp}`: link, DNS, UDP, TCP); the simulator backend uses real BSD sockets. `TARGET_OPTA` appears only under `hal/`; shared code never branches on it (`TARGET_SIMULATOR` may, for dev-only scaffolding).

**Terminal UI (`terminal/`)** - `AnsiTerminal` owns the serial connection and drives a multiplexed page display. `InputParser` decodes raw byte streams into `InputEvent`s (handles X3.64 CSI, Alt, and function-key escape sequences). `InputEditor` is a line-editing state machine that consumes `InputEvent`s. `Ansi` is a stateless helper that emits ANSI escape codes.

**Pages (`pages/`)** - Each page (`Home`, `Shell`, `Logs`, `Status`) implements the `Page` interface. `AnsiTerminal` routes input events to the active page and calls each page's render method when the display needs updating.

**Logging (`logs/`)** - `Logger` uses a dual-bucket token-bucket rate limiter (one global bucket, one per topic) before forwarding to registered `LogWriter`s. `LOG_CRITICAL` entries are undroppable (they evict the oldest queued entry instead of being dropped), and silent-loss categories (sink drops, TX-full truncation, rate-limiter suppressions) are reported edge-triggered as criticals. Writers include `BufferedLogWriter` and `SerialLogWriter`. Log topics are the `LogTopic` enum in `plcio/vars.h` (`PLC_IO`, `BOOT`, `NETWORK`, `TIME`, `STATUS`, and the firmware-only `ASSERT`).

**PLC I/O (`plcio/`)** - `PlcSyncHandler` manages two independent state machines (RX and TX) for shared-memory communication with the PLC coprocessor. Uses explicit ARM barriers (`HAL::dsb()`, plus `HAL::ordered_read()` for cross-core counters), D-cache maintenance at the `step()` boundaries (`HAL::cache_invalidate`/`cache_clean`, no-ops on the simulator), and transactional read/write patterns to prevent torn operations; a torn-read diagnostic (`rx_torn_count()`) flags any handshake violation. Timing (all in `libbotanik_config.h`): heartbeat `PLC_HEARTBEAT_MS` (250 ms), link timeout `PLC_TIMEOUT_MS` (2.5 s), software watchdog `PLC_WATCHDOG_MS` (5 s). On a sustained fault, `HealthMonitor` keeps kicking the hardware watchdog through a `PLC_FAULT_GRACE_MS` (15 s) grace, then withholds the kick so the `HW_WATCHDOG_MS` (10 s) hardware watchdog resets both cores.

**Health (`health/`)** - `HealthMonitor` owns the `AlarmModel` and an `MqttAlarmNotifier`, asserts the platform-health alarms (`alarms/platform.h`) from the PLC status and the loop's own liveness, runs a power-on self-test, and gates the watchdog kick via the timed escalation above. It reads the PLC's raw alarm bitset plus the live `rx_torn_count()` each beat.

**Utils (`utils/`)** - `BufferedSerial` wraps UART with a 256-byte RX ring buffer and 4096-byte TX buffer (usable depth size-1: 255 / 4095). The ring is single-producer/single-consumer but main-loop-only (no serial ISR), so it carries no memory barriers. `time.h` provides uptime tracking on top of HAL.


### Key compile-time defines

| Define | Set when |
|---|---|
| `TARGET_SIMULATOR` | SIMULATOR build |
| `DEBUG` | SIMULATOR build |
| `_GNU_SOURCE` | SIMULATOR build |
| `TARGET_OPTA` | OPTA build |
| `ARDUINO` / `MBED_*` | OPTA build |

### Optional features (`BOTANIK_FEATURE_*`)

`libbotanik` is usable without a PLC. Optional subsystems are gated at compile time by `BOTANIK_FEATURE_*` flags in `src/libbotanik_features.h`, which mirrors the `BOTANIK_*` config-override pattern: each defaults from an `#ifndef`-guarded macro, a site can `#define` it in `sites/<name>/config.h`, and the build can set `-DBOTANIK_FEATURE_<X>=ON/OFF` (forwarded PUBLIC on the `botanik` library, same ABI discipline as `BOTANIK_SITE_CONFIG`).

| Flag | Default | Gates |
|---|---|---|
| `BOTANIK_FEATURE_PLC` | on | `PlcSyncHandler`, `PlcLogReader`, the `<In, Out>` payload threaded through `BotanikFirmware`, and the PLC-fault watchdog escalation |
| `BOTANIK_FEATURE_NETWORK` / `_NTP` / `_MQTT` | off | the `network/` stack - reserved (declared but not yet instantiated); `NTP`/`MQTT` require `NETWORK`, `NETWORK` requires `PLC` (enforced by `#error`) |
| `BOTANIK_FEATURE_TERMINAL` | on | the ANSI UI - reserved for parity, not yet gated |

Gate with the preprocessor `#if BOTANIK_FEATURE_PLC` (must drop member decls / ctor-init entries; C++11 has no `if constexpr`); a `Botanik::Features::PLC` `constexpr bool` mirror exists for expression context. To build PLC-less: `-DBOTANIK_FEATURE_PLC=OFF` and derive from `BotanikFirmware<>` (the payload params default to a zero-size `Empty`). `HealthMonitor` already tolerates a null PLC link (POST skips the region check, the watchdog keeps kicking while the loop is alive). See the "Optional features" section of the docs Library page (`docs/library.dox`).

## Site-specific code (greenhouse targets)

`libbotanik` is generic and site-independent. Code specific to one physical greenhouse and its PLC program (the shared-memory payload layout, its pages, its alarm table, and the mock data that animates it) is a **site**, code-named per greenhouse. The reference site is `gh_mlb41`.

Three layers, generic to specific:

| Layer | Holds | Examples |
|---|---|---|
| `libbotanik` | generic, site- and target-independent machinery, plus the `BotanikFirmware<In, Out>` base that owns the generic components, exposes a reference to each on the instance, and drives the per-tick `loop()` | `PlcSyncHandler`, `AnsiTerminal`, `AlarmModel`, `BotanikFirmware` |
| site (`gh_mlb41`) | single source of truth for one greenhouse: the `PlcInput/OutputData_t` layout, site pages (Home/Status), the `g_alarms[]` table, the `fill_output()` data simulation, and a `GreenhouseMLB41` class deriving from `BotanikFirmware` | `gh_mlb41` |
| target / entry point | thin: selects one site and runs it | `botanik_sim` harness, generated OPTA `.ino` |

**Composition.** Components are split by layer and chained by inheritance (not an ideal composition model, but a structured start):

- `BotanikFirmware<In, Out>` constructs the generic components (logger, scheduler, terminal, PLC handler, log reader, alarm reader, alarm model, core pages) and exposes a reference to each on the instance.
- `GreenhouseMLB41 : BotanikFirmware<In, Out>` declares its site components (site pages, the alarm table, the shell-command table, any site logic) as members, built from the base's exposed component references.
- A site component is often a static site-defined table plus an opaque ctx, registered on a base-owned generic component: the site `g_alarms[]` table feeds `AlarmModel` (which splices the generic platform-health alarms from `libbotanik/alarms/platform.h` into the top of the bitset, so site/PLC alarms fill from bit 0 and platform alarms sit at the high end), a `ShellCommand[]` table registers on the shell page via `set_commands()`.
- Lifecycle chains up the hierarchy: `GreenhouseMLB41::setup()` calls `BotanikFirmware::setup()` first, then wires its own; same for `loop()`.
- The Arduino sketch may add one more slim subclass for last-mile customization, chaining `setup()` / `loop()` the same way.

Rules:

- **One data layout per site, shared by simulator and hardware.** A site's `PlcOutputData_t` / `PlcInputData_t` is used verbatim by both the OPTA firmware and the simulator's mock PLC. Never fork the layout per target; evolve the layout and its `fill_output()` animator together, in the site.
- **The simulator is a generic harness.** It allocates the shared region locally and drives a `PlcEmulator` with the selected site's `fill_output()`. On OPTA the real coprocessor plays that role and the region comes from `HAL::get_plc_input()/get_plc_output()`; the site code is identical either way.
- **The site stays portable.** Site code must cross-compile as an Arduino library: the deploy step zips the site dir and generates a `.ino` that statically allocates a `GreenhouseMLB41` and calls its `setup()` / `loop()`. The zip build does not define `TARGET_SIMULATOR`, so simulator-only code (the `fill_output()` animator, the emulator thread) drops out through its `#ifdef TARGET_SIMULATOR` guard. Keep such code guarded rather than in the portable path.
- **Generic components constrain the site payload.** Templated generics require fields on the site `PlcOutputData_t` (e.g. `AlarmReader` / `StatusPage` need a 64-bit `alarms` bitset). The site defines its layout to satisfy those contracts.
- **Config is site-overridable.** Every `libbotanik_config.h` value defaults from an `#ifndef`-guarded `BOTANIK_<NAME>` macro. A site may ship `sites/<name>/config.h` that `#define`s any of them; CMake includes it via `BOTANIK_SITE_CONFIG`, defined PUBLIC on the `botanik` library so the override is build-wide (libbotanik's own TUs and every consumer see the same value - anything narrower is an ABI mismatch). The `LOG_PLC_*` sizes are the canonical case (they must match the site's PLC program).
- New site-level work goes in the site, not in `libbotanik` and not in a target's `main`.

**Current state (structural migration done):** the `gh_mlb41` site lives in `src/sites/gh_mlb41/`, the `BotanikFirmware<In, Out>` base in `src/libbotanik/firmware.h`, and `GreenhouseMLB41` derives it. The site is a small set of files: `site.h` holds the PLC payload (the `PlcInput/OutputData_t` layout + its status enums and their string companions) **and** the `GreenhouseMLB41` subclass; `pages.h` the site pages; `alarms.h`/`alarms.cpp` the alarm table; `sim.h` the header-only mock PLC (its body `#ifdef TARGET_SIMULATOR`); `config.h` the build-wide config overrides (kept separate so CMake can include it before the library defaults). A site does **not** override `setup()`/`loop()`; it customizes through the protected Template-Method hooks: `register_pages()` (tab order, page 0 = boot page), `register_commands()` (its shell command table), `on_setup()` (post-wiring), and `on_loop()` (per-beat). The alarm table is passed to the base constructor. See the worked extension example in `firmware.h`. Both targets run the same site: `botanik_sim/main.cpp` is a thin harness owning local PLC statics, and `botanik_fw` (OPTA) binds `GreenhouseMLB41` to `HAL::get_plc_input()/get_plc_output()` (cross-built clean). The site is chosen at configure time with `-DBOTANIK_SITE=<name>` (default `gh_mlb41`): CMake globs `src/sites/<name>/` and points `BOTANIK_SITE_HEADER` at its `site.h`, which exposes `BotanikSite` + the payload, so the harnesses never name the site. Only the Arduino `.zip`/`.ino` deploy packaging remains (carry the site dir in the zip, generate the integrator entry once the Axel hooks are confirmed). Treat `src/sites/gh_mlb41/` + `BotanikFirmware` as canonical.
