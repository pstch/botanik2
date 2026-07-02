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

**Build type & sanitizers.** Builds default to `DEBUG` (`-O0 -g3`, profiler-friendly).
DEBUG SIMULATOR builds also default `BOTANIK_SANITIZE=address+undefined` (ASan+UBSan,
recoverable so the interactive sim stays usable); turn off with `-DBOTANIK_SANITIZE=OFF`.
For an optimized build pass `-DCMAKE_BUILD_TYPE=RELEASE` (`-O3` + LTO, `NDEBUG`,
stripped, sanitizers off). The `scheduler.cpp` hot path is compiled with
`-Wdouble-promotion` to keep the per-beat metric math single-precision (no double FPU
on the M4).

## Tests & checks

Host-only, SIMULATOR build. There are two suites, both wired into CTest:

- **doctest** C++ unit tests in `tests/` (vendored single header `tests/vendor/doctest.h`),
  one CTest entry per `TEST_CASE` via `doctest_discover_tests`.
- **pyte** Python screen-assertion tests in `tests/pyte/` that drive `botanik_sim` over a
  PTY and assert its rendered ANSI screen against golden captures. Registered only when
  `pytest`+`pyte` are importable; they run against a dedicated RELEASE no-sanitizer sim
  (timing budget). Regenerate goldens with `make -C build pytest-update` (review the diff;
  never part of the gate).

```bash
make -C build check                  # the one-shot pre-push gate (see below)
make -C build botanik_tests          # just build the doctest binary
./build/tests/botanik_tests          # run it directly (doctest CLI)
./build/tests/botanik_tests -s       # ...show every passing check + the struct-size table
./build/tests/botanik_tests -tc=<name>   # run a single test case
ctest --test-dir build --output-on-failure          # all CTest entries
ctest --test-dir build -R pyte --output-on-failure  # just the pyte suite
```

`make check` is the pre-push gate; it fails on the first problem:
- **`botanik_strict`** - a `-Werror` mirror of the library (plain `make` stays
  warning-tolerant; this gate fails on any warning).
- **clang-tidy** - the `bugprone-*` checks from `.clang-tidy`, promoted to errors
  (only present when `run-clang-tidy` was found at configure time).
- the **doctest + pyte** suites via `ctest`.

A separate `make -C build cppcheck` target runs cppcheck when installed.

## Architecture

Everything is non-blocking: every stateful object exposes a `step()`/`loop()` doing a
small bounded amount of work and returning immediately; the main loop calls them in a
fixed order (blocking would stall the RTOS and trip the watchdog on the OPTA). Code
reaches the platform only through HAL, never raw `<time.h>` or platform APIs.

The library subsystems under `src/libbotanik/`:

**HAL (`hal/`)** - `platform.h` defines the unified interface, grouped into nested namespaces by concern: `Clock` (`current_ms()`, `uptime_ms()`, `wall_ms()`, `sleep_ms()`), `Memory` (`dsb()`, `dmb()`, `ordered_copy/move/read()`, `cache_clean/invalidate()`), `Stack` (`base/depth/high()`), `PlcIO` (`input/output()`), `Debug` (`breakpoint()`, `under_debugger()`), `System` (`reset()`, `shutdown()`), `Watchdog` (`start/kick/stop()`), `Serial` (`Raw`, `Port`, `get()`), and `Network` (the transport, in `hal/net.{h,cpp}`). `hardware_description()` stays at the `HAL::` top level. `simulator.h` implements it with `std::chrono` and Linux serial I/O; `opta.h` implements it for Mbed OS. All other code uses HAL exclusively - never raw `<time.h>` or platform-specific calls. The network transport (`hal/net.{h,cpp}`: link, DNS, UDP, TCP) is `HAL::Network`, mirroring the library's `Botanik::Network` components; the simulator backend uses real BSD sockets. `TARGET_OPTA` appears only under `hal/`; shared code never branches on it (`TARGET_SIMULATOR` may, for dev-only scaffolding).

**Scheduler (`scheduler/`)** - `LoopScheduler` is the non-blocking fixed-period loop driver: it releases one tick per period, handles overruns, and self-instruments per-beat (CPU duty cycle `(period - slept)/period`, scheduling jitter, work duration, and stack high-water via stack painting), publishing the metrics into a `SchedulerContext` surfaced in the status bar and the `stats` shell command.

**Firmware base (`core/firmware.h`)** - `BotanikFirmware<In, Out>` is the Template-Method base that constructs and owns every generic component, fixes the non-overridable per-tick `loop()` order (`tick -> receive -> plc -> health -> on_loop -> logger -> terminal`), and exposes a reference to each component. Sites customize only through protected hooks: `register_pages()`, `register_commands()`, `on_setup()`, `on_loop()` (see the Site section).

**Component / context (`core/component.h`, `core/context.h`)** - `Component` is a mixin base giving each subsystem a one-shot first-step BOOT/DEBUG announce. `SystemContext` is a borrowed-pointer aggregate of per-subsystem status contexts (scheduler, PLC, ...) the terminal UI reads to render.

**Config handler (`core/config.h`)** - `ConfigHandler` is a base for PLC-config-driven state machines that reset and restart whenever the operator bumps their config block's serial (template-method order: read serial -> reset on change -> advance). `SunPositionTracker` and the network handlers build on it.

**Terminal UI (`terminal/`)** - `AnsiTerminal` owns the serial connection and drives a multiplexed page display. `InputParser` decodes raw byte streams into `InputEvent`s (handles X3.64 CSI, Alt, and function-key escape sequences). `TextEditor`/`InputEditor` is a byte-efficient line-editing state machine that consumes `InputEvent`s. `Ansi` is a stateless helper that emits ANSI escape codes. The layer minimizes escape bytes emitted (low-bandwidth serial link).

**Pages (`pages/`)** - Full-screen views behind a common `Page` interface; `AnsiTerminal` routes input to the active page and calls its render method on demand. Core pages: `Home`, `Shell`, `Logs`, `Status`, `AlarmsPage` (scrollable alarm-condition table, filter modes recent/all/active/unmasked). Building blocks: `MetricPage` (diff-rendered metric display base with value cache + watch-style highlighting), `Scrollback<L>` (throttled incremental-redraw circular buffer) over `LineBuffer<L,N>` (fixed-capacity circular storage with soft-wrap).

**Shell (`shell/`)** - `CommandShell` handles command submission, built-in dispatch (`help`, `clear`, `logs`, `stats`, `version`, ...), command history (up/down recall), and a kill-ring (Ctrl-K/Ctrl-Y). Sites extend it with a registered `ShellCommand[]` table.

**Logging (`logs/`)** - `Logger` uses a dual-bucket token-bucket rate limiter (one global bucket, one per topic) before forwarding to registered `LogWriter`s. `LOG_CRITICAL` entries are undroppable (they evict the oldest queued entry instead of being dropped), and silent-loss categories (sink drops, TX-full truncation, rate-limiter suppressions) are reported edge-triggered as criticals. Writers include `BufferedLogWriter` and `SerialLogWriter`. Log levels and topics are the `LogLevel` / `LogTopic` enums in `logs/topics.h` (logging primitives usable without the PLC; their values stay the wire protocol the PLC log ring carries). Topics: the wire-shared `PLC_IO`/`BOOT`/`NETWORK`/`TIME`/`STATUS`, plus C-side-only topics the PLC never emits (`ASSERT`, the alarm transitions `ALARM_START`/`ALARM_END`/`ALARM_FLAP`, and `HEALTH`).

**PLC I/O (`plcio/`)** - `PlcSyncHandler` manages two independent state machines (RX and TX) for shared-memory communication with the PLC coprocessor. Uses explicit ARM barriers (`HAL::Memory::dsb()`, plus `HAL::Memory::ordered_read()` for cross-core counters), D-cache maintenance at the `step()` boundaries (`HAL::Memory::cache_invalidate`/`cache_clean`, no-ops on the simulator), and transactional read/write patterns to prevent torn operations; a torn-read diagnostic (`rx_torn_count()`) flags any handshake violation. The PLC->host region carries a generic `PlcOutputConfig_t` config block (`plcio/config.h`: network/NTP/MQTT) between the log ring and the site payload - resizing it is a coordinated wire-ABI change. Timing (all in `libbotanik_config.h`): heartbeat `PLC_HEARTBEAT_MS` (250 ms), link timeout `PLC_TIMEOUT_MS` (2.5 s), software watchdog `PLC_WATCHDOG_MS` (5 s). On a sustained fault, `HealthMonitor` keeps kicking the hardware watchdog through a `PLC_FAULT_GRACE_MS` (15 s) grace, then withholds the kick so the `HW_WATCHDOG_MS` (10 s) hardware watchdog resets both cores.

**Alarms (`alarms/`)** - `AlarmModel` merges the remote (PLC) and local alarm bitsets, tracks dwell, and raises ALARM-topic log lines and MQTT notifications on thresholds. `AlarmReader<OutputData>` reads the PLC alarm field each loop and drives the model. `MqttAlarmNotifier` formats alarm transitions as MQTT topics/payloads. `platform.h` defines the generic platform-health alarms (CPU load, jitter, stack, overrun), spliced into the high end of the bitset so site/PLC alarms keep bit 0 upward.

**Health (`health/`)** - `HealthMonitor` owns the `AlarmModel` and an `MqttAlarmNotifier`, asserts the platform-health alarms (`alarms/platform.h`) from the PLC status and the loop's own liveness, runs a power-on self-test, and gates the watchdog kick via the timed escalation above. It reads the PLC's raw alarm bitset plus the live `rx_torn_count()` each beat. Tolerates a null PLC link (POST skips the region check, watchdog keeps kicking while the loop is alive).

**Network (`network/`)** - Wired into `BotanikFirmware` behind `BOTANIK_FEATURE_NETWORK` / `_NTP` / `_MQTT` (default off; on for `gh_mlb41`). Functional on OPTA; on the simulator the `hal/net.cpp` backend is hermetic - it emulates DNS/NTP/MQTT in-process (a name resolves to a fixed mock peer after a short async delay, NTP queries get a canned reply, MQTT connects) so the handlers run their real state machines against a deterministic, offline peer with no host network I/O. `NetworkHandler` brings up the network device and hands it to `NameResolver` (non-blocking DNS over Mbed's async resolver, poll-style API + cache); `NtpClient` runs NTP through the resolver; `MqttClient` is a minimal non-blocking MQTT 3.1.1 publisher.

**Sun tracking (`sun/`, gated by `BOTANIK_FEATURE_SUN`)** - in the `Botanik::Sun` namespace: `Sun::SunPositionTracker` (`sun/sun_tracker.h`) is a config-driven `Component` tracking next sunrise/sunset and day/night state for the site location; `sun/sun.h` is the solar-position math (NOAA equations, declination, event times in the uptime-ms domain). Standalone (not instantiated by `BotanikFirmware`); default on, `-DBOTANIK_FEATURE_SUN=OFF` drops the `sun/` sources and their tests.

**Utils (`utils/`)** - `BufferedSerial` wraps UART with a 256-byte RX ring buffer and 4096-byte TX buffer (usable depth size-1: 255 / 4095). The ring is single-producer/single-consumer but main-loop-only (no serial ISR), so it carries no memory barriers. `filters.h` holds reusable EMA / windowed-max metric filters; `time.h` provides uptime tracking on top of HAL; `text.h` the `format_float`/`format_age` helpers; `assert.h` the firmware-only assert.


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
| `BOTANIK_FEATURE_NETWORK` / `_NTP` / `_MQTT` | off | the `network/` stack (`NetworkHandler`/`NameResolver`/`TimeSyncHandler`/`MqttClient`), instantiated + stepped by `BotanikFirmware`; on for `gh_mlb41`. `NTP`/`MQTT` require `NETWORK`, `NETWORK` requires `PLC` (enforced by `#error`) |
| `BOTANIK_FEATURE_CONSOLE` | on | the interactive ANSI UI: `AnsiTerminal`, the built-in shell/logs/alarms pages, the console UART, and the `register_pages()` / `register_commands()` hooks. Off -> headless |
| `BOTANIK_FEATURE_MONITOR` | on | the secondary MONITOR UART and its `SerialLogWriter` sink. Independent of `CONSOLE`: a headless build can keep MONITOR for diagnostics, an interactive build can drop it. Both off -> logs reach only site-registered sinks |

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
- **The simulator is a generic harness.** It allocates the shared region locally and drives a `PlcEmulator` with the selected site's `fill_output()`. On OPTA the real coprocessor plays that role and the region comes from `HAL::PlcIO::input()/output()`; the site code is identical either way.
- **The site stays portable.** Site code must cross-compile as an Arduino library: the deploy step zips the site dir and generates a `.ino` that statically allocates a `GreenhouseMLB41` and calls its `setup()` / `loop()`. The zip build does not define `TARGET_SIMULATOR`, so simulator-only code (the `fill_output()` animator, the emulator thread) drops out through its `#ifdef TARGET_SIMULATOR` guard. Keep such code guarded rather than in the portable path.
- **Generic components constrain the site payload.** Templated generics require fields on the site `PlcOutputData_t` (e.g. `AlarmReader` / `StatusPage` need a 64-bit `alarms` bitset). The site defines its layout to satisfy those contracts.
- **Config is site-overridable.** Every `libbotanik_config.h` value defaults from an `#ifndef`-guarded `BOTANIK_<NAME>` macro. A site may ship `sites/<name>/config.h` that `#define`s any of them; CMake includes it via `BOTANIK_SITE_CONFIG`, defined PUBLIC on the `botanik` library so the override is build-wide (libbotanik's own TUs and every consumer see the same value - anything narrower is an ABI mismatch). The `LOG_PLC_*` sizes are the canonical case (they must match the site's PLC program).
- New site-level work goes in the site, not in `libbotanik` and not in a target's `main`.

**Current state (structural migration done):** the `gh_mlb41` site lives in `src/sites/gh_mlb41/`, the `BotanikFirmware<In, Out>` base in `src/libbotanik/core/firmware.h`, and `GreenhouseMLB41` derives it. The site is a small set of files: `site.h` holds the PLC payload (the `PlcInput/OutputData_t` layout + its status enums and their string companions) **and** the `GreenhouseMLB41` subclass; `pages.h` the site pages; `alarms.h`/`alarms.cpp` the alarm table; `sim.h` the header-only mock PLC (its body `#ifdef TARGET_SIMULATOR`); `config.h` the build-wide config overrides (kept separate so CMake can include it before the library defaults). A site does **not** override `setup()`/`loop()`; it customizes through the protected Template-Method hooks: `register_pages()` (tab order, page 0 = boot page), `register_commands()` (its shell command table), `on_setup()` (post-wiring), and `on_loop()` (per-beat). The alarm table is passed to the base constructor. See the worked extension example in `core/firmware.h`. Both targets run the same site: `botanik_sim/main.cpp` is a thin harness owning local PLC statics, and `botanik_fw` (OPTA) binds `GreenhouseMLB41` to `HAL::PlcIO::input()/output()` (cross-built clean). The site is chosen at configure time with `-DBOTANIK_SITE=<name>` (default `gh_mlb41`): CMake globs `src/sites/<name>/` and points `BOTANIK_SITE_HEADER` at its `site.h`, which exposes `BotanikSite` + the payload, so the harnesses never name the site. Only the Arduino `.zip`/`.ino` deploy packaging remains (carry the site dir in the zip, generate the integrator entry once the Axel hooks are confirmed). Treat `src/sites/gh_mlb41/` + `BotanikFirmware` as canonical.
