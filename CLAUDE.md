# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this 
repository.

## Project overview

Botanik is a greenhouse control system for an Arduino OPTA PLC (STM32H747 dual-core ARM). Botanik runs on the M7 core; it manages irrigation, ventilation, cooling, and monitoring. The core is `libbotanik`, a C++ library designed to work around Arduino OPTA platform limitations. It builds either as a Linux simulator (`SIMULATOR` target) or for the actual hardware (`OPTA` target).

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

The repo ships a `.envrc` exporting `MAKEFLAGS=-j4` and `CTEST_PARALLEL_LEVEL`, so
`make` builds in parallel and a bare `ctest` runs cases concurrently (~6 s against ~14 s)
once [direnv](https://direnv.net) loads it. Without direnv, export `MAKEFLAGS` yourself or
pass `-j` per build. `ccache` is used automatically when installed, so a clean rebuild
after `rm -rf build` replays cached objects in seconds.

OPTA builds use `arm-none-eabi-gcc/g++` and the Arduino Mbed OPTA core:

```bash
arduino-cli core install arduino:mbed_opta
```

CMake auto-discovers the highest version installed under `~/.arduino15`; override with
`-DMBED_CORE_ROOT=...` or the `ARDUINO_DATA_DIR` env var.

**Build type & sanitizers.** Builds default to `DEBUG` (`-O0 -g3`). Sanitizers default
**OFF** on every build type - instrumentation is the single largest cost in this tree, and
it lands hardest on the test TUs, which are ~70% of all compile time. They live in
`make check-full`, which builds its own instrumented tree under `build/asan/`. Instrument
a whole tree with `-DBOTANIK_SANITIZE=address+undefined`
(OFF | address | undefined | address+undefined); `-DCMAKE_BUILD_TYPE=RELEASE` gives `-O3`
+ LTO, `NDEBUG`, stripped.

**Warnings are errors** on the library's own sources (`BOTANIK_WERROR`, default ON;
`-DBOTANIK_WERROR=OFF` for a WIP tree). PRIVATE to `botanik`, so the sketch and the test
suite keep `-Wall -Wextra` without `-Werror`. The `scheduler.cpp` hot path adds
`-Wdouble-promotion`, keeping the per-beat metric math single-precision (no double FPU on
the M4).

## Tests & checks

Host-only, SIMULATOR build. Two suites, both wired into CTest:

- **doctest** unit tests in `tests/` (vendored `tests/vendor/doctest.h`), registered
  TWICE: one unlabelled entry running the whole binary (what the gate uses), and one
  `unit`-labelled entry per `TEST_CASE` so an IDE or `ctest -R` can address one case.
- **pyte** screen tests in `tests/pyte/`, driving `botanik_sim` over a PTY and asserting
  its rendered ANSI against golden captures. Registered only when `pytest`+`pyte` import.
  Regenerate goldens with `make -C build pytest-update` - review the diff; never in the gate.

```bash
make -C build check                  # the pre-push gate
make -C build check-full             # the periodic sweep: clang-tidy, then the gate under ASan+UBSan
make -C build botanik_tests          # just build the doctest binary
./build/tests/botanik_tests          # run it directly (doctest CLI)
./build/tests/botanik_tests -s       # ...every passing check + the struct-size table
./build/tests/botanik_tests -tc=<name>              # one test case
ctest --test-dir build --output-on-failure          # all CTest entries
ctest --test-dir build -R pyte --output-on-failure  # just the pyte suite
```

**`make check`** fails on the first problem: the library compiles `-Werror`, so building
the suite at all fails on a warning in a library source; then the doctest + pyte suites
via `ctest -LE unit`. The gate excludes the per-case entries because the aggregate entry
covers the same cases in one process (~0.6 s against ~14 s, all of it process startup),
and one process is also what catches a case corrupting state for the ones after it.

On a doctest failure the gate (`tests/gate.cmake`) re-runs the per-case entries itself, so
each failing case is named - paid only when something is broken. **If that re-run comes
back all-green the fault is INTERFERENCE between cases, not a flake**: reproduce it with
`./build/tests/botanik_tests`, the one-process configuration.

**`make check-full`** runs clang-tidy over `src/` first, so a finding stops it before it
pays for an instrumented rebuild, then the whole `check` gate again in `build/asan/`. It
drives that tree's own `check` target, so the two cannot drift. Both halves are
instrumented, screen tests included.

**clang-tidy is not in `check`** - it re-lints on any touched header, too slow to pay per
edit - but it IS the first half of `check-full`.

```bash
make -C build clang-tidy       # incremental: one stamp per TU under build/clang-tidy/
rm -rf build/clang-tidy && make -C build clang-tidy   # the whole tree, ignoring stamps
```

It runs the `bugprone-*` checks from `.clang-tidy` with findings promoted to errors, and
exists only when clang-tidy was found at configure time AND the generator is Make (the
stamps' header tracking is the Makefile generator's own dependency scanner).
Re-configuring, or editing `.clang-tidy`, invalidates every stamp.

The pyte suite drives `botanik_sim`, which a plain `make` builds - CTest itself never
builds anything, and CMake's built-in `test` target cannot be given dependencies. A screen
test that finds the sim missing fails loudly rather than skipping, which CTest would
report as `Passed`.

## Comments and prose

Full rules in `CONTRIBUTING.md` § Documentation. The governing one:

> **Comments say what the code cannot: units, ownership, sentinels, wire facts,
> hardware quirks, invariants. Everything else - rationale, alternatives considered,
> history, architecture - goes to `docs/dev/`, or is deleted.**

If a comment would still be true after the function was rewritten a different way, it
is rationale and does not belong next to the code. Prefer readable code over a comment
explaining unreadable code.

- `@brief` is 1-2 lines (hard cap 3); a whole doc block is <= 6 lines; at most one
  `@note`. No `@code` and no `@section` in a header - those go to `docs/dev/`.
- `@param`/`@return` only for a non-obvious unit, range, ownership or sentinel. Never
  to restate the parameter name.
- Implementations: non-obvious invariants, quirks and workarounds only. No banners, no
  comment restating the next line.
- Tests: the test name is the documentation. No header comment above a `TEST_CASE`.
- Prose: no "not X but Y" constructions, no history (`git log` has it), no em-dashes.

Rough budgets for review, not a gate: prose lines <= **50%** of code lines in a header,
<= **15%** in a `.cpp` or a test. Headers get more room because every public entity
carries a `@brief`; `@name`/`@{`/`@}` scaffolding does not count. `vars/model.h` and
`hal/platform.cpp` sit above budget legitimately - see `CONTRIBUTING.md`.

## Architecture

**Everything is non-blocking.** Every stateful object exposes a `step()`/`loop()` doing a
small bounded amount of work and returning immediately; the main loop calls them in a
fixed order. Blocking stalls the RTOS and trips the watchdog on the OPTA.

**Code reaches the platform only through HAL** - never raw `<time.h>` or platform APIs.
`TARGET_OPTA` appears only under `hal/`; shared code never branches on it
(`TARGET_SIMULATOR` may, for dev-only scaffolding).

Subsystems under `src/libbotanik/`. Each row's doc is the reference; the header is the API.

| Directory | What it is | Detail |
|---|---|---|
| `hal/` | `platform.h` is the unified interface, in nested namespaces by concern: `Clock`, `Memory`, `Stack`, `Debug`, `System`, `Watchdog`, `Serial`, `Env`, `Storage`, `Network`. `simulator.h` / `opta.h` are the backends; each carries only what must be INLINE, everything cold living in `platform.cpp` | `porting.md` |
| `scheduler/` | `LoopScheduler`, the fixed-period loop driver: releases one tick per period, handles overruns, self-instruments per beat (CPU duty, jitter, work duration, stack high-water) into a `SchedulerContext` | `scheduler.md` |
| `tasks/` | The task table and `TaskTimer`. `LoopScheduler` decides WHEN the beat happens, `Tasks::Registry` decides WHAT runs in it. Sits BELOW `core/` | `tasks.md` |
| `core/` | The BOTTOM of the stack: `Component` (one hook per phase, `register_tasks()`) and `SystemContext` (a borrowed-pointer aggregate, constructor-injected into its consumers, NOT reachable through the console) | `architecture.md` |
| `firmware/` | `BotanikFirmware`, the Template-Method base owning every generic component and the non-overridable per-tick order. Sites customize only through the protected hooks | `architecture.md` |
| `vars/` | The one model everything the firmware exposes goes through, along three independent fields: `VarType`, `Transport`, `VarRole`, plus `VarScope` for what a table filters on. `Bank` is the one object that gets, sets and overrides | `vars.md` |
| `plc/` | The POU vocabulary, split as IEC 61131-3 splits it: `functions/` has no state, `blocks/` has state. A function is a single expression in a header; a block needs a `.cpp` | `scan.md` |
| `modbus/` | Four corners in one tree: `rtu/` is the RS-485 field bus, `tcp/` the plant network, each with both ends. `wire.h` and `response.h` are what they share | `modbus.md` |
| `console/` | Two layers: `AnsiTerminal` is the VT device owning a screen back-buffer, `Console` the multiplexed page display. `TextEditor` is the byte-efficient editing state machine | `screen.md`, `editor.md`, `input.md` |
| `pages/` | Full-screen views behind a common `Page` interface, all base-owned and site-independent | `pages.md` |
| `shell/` | `CommandShell`: commands are **noun then verb**, a noun parsing its own verb. Depends on the terminal device, not on `Console` or `pages/` | `pages.md` |
| `logs/` | `Logger` with a dual-bucket token-bucket rate limiter before its `LogWriter`s. `LOG_CRITICAL` is undroppable | `logging.md` |
| `alarms/` | There is no site alarm table: an alarm is a `SCOPE_A` row in the variable table carrying both its boolean and its dwell policy. `AlarmController` scans the bank in table order | `health.md` |
| `health/` | `HealthMonitor` owns the `AlarmController`, raises the platform-health alarms from scheduler metrics, runs the POST, and produces the watchdog verdict | `health.md` |
| `network/` | `NetworkHandler` / `NameResolver` / `NtpClient` / `MqttClient`, all non-blocking. The sim backend is hermetic - it emulates DNS/NTP/MQTT in-process | `network.md` |
| `sun/` | `SunPositionTracker` over the NOAA solar-position math. Standalone: a site owns the instance | - |
| `utils/` | `BufferedSerial` (256-byte RX ring, 4096-byte TX; not a Component and pumped by whoever owns the wire), metric filters, uptime, text helpers, the firmware assert | `concurrency.md` |

### Rules that are not in a header

- **Control authority** is three modes (`Vars::ControlMode`) decided by two INDEPENDENT
  sources - the operator's standing choice and the system's hold - and the STRICTER wins.
  `CONTROL_DISABLED` *mutes* an output override reversibly; only the SYSTEM's hold ERASES
  one. It starts held, so nothing before the end of `setup()` can energise an actuator.
  See `scan.md`.
- **The scan cycle has no driver.** A control program registers itself with
  `Tasks::Registry` like everything else, PHASE_CONTROL by construction (`add_program()`).
  A program due late runs ONCE - missed activations are dropped, never caught up - so
  `dt_ms` is always the ACTUAL elapsed time. `first_scan` is where a program drops what
  the outage made stale. See `scan.md`.
- **Library rows.** The library declares rows of its own into the site's one contiguous
  table through `BOTANIK_LIBRARY_VARS`, placed FIRST in `g_vars[]`. Two ordering rules,
  both from the contiguous `ROLE_PARAM` run: the block goes first, and its parameters go
  last within it. Registers 900-999 are the library's. **Whatever the library reads by
  name, the library declares.** See `vars.md`.
- **Analog calibration.** An `AI`/`AO` row points at a shared `const AnalogCal` of six
  `Tunable`s; `scale`/`offset` are never stored but derived, so a recalibration lands the
  moment the parameter commits. Anything DISPLAYING a band goes through `var_band()`, not
  `num_band()`. See `vars.md`.
- **Naming files a variable.** `var_group()` is the prefix of a name up to the first `_`;
  `var_published()` is true iff a row is neither a parameter nor an alarm. There is no
  group table to add a variable to and none to forget.

### Key compile-time defines

| Define | Set when |
|---|---|
| `TARGET_SIMULATOR` | SIMULATOR build |
| `DEBUG` | SIMULATOR build |
| `_GNU_SOURCE` | SIMULATOR build |
| `TARGET_OPTA` | OPTA build |
| `ARDUINO` / `MBED_*` | OPTA build |

### Optional features (`BOTANIK_FEATURE_*`)

Optional subsystems are gated at compile time by the 24 `BOTANIK_FEATURE_*` flags in `src/libbotanik_features.h`, which mirrors the `BOTANIK_*` config-override pattern: each defaults from an `#ifndef`-guarded macro, a site can `#define` it in `sites/<name>/config.h`, and the build can set `-DBOTANIK_FEATURE_<X>=ON/OFF` (forwarded PUBLIC on the `botanik` library, same ABI discipline as `BOTANIK_SITE_CONFIG`).

**One roster, two hand-edits.** `BOTANIK_FEATURE_LIST` (an X-macro in the header) is the single source of truth for which flags exist: the `features` shell command expands it, and CMake forwards `-D`s by matching the `BOTANIK_FEATURE_` prefix over its cache rather than from a list. Both used to be hand-maintained copies, and the CMake one *silently ignored* the `-D` of any flag missing from it - which is exactly what happened when NOTES was added. Adding a flag is now an `X()` entry plus an `#ifndef` default, and a row in `docs/integration/config.md`.

**Defaults are a minimal core, opt in**: a build that sets nothing gets `CONSOLE` + `HOME_PAGE` + `SHELL_PAGE` + `LOGGING` + `PLC_SCAN`. Everything else is off. `src/sites/gh_mlb41/config.h` is the worked full profile, and because the test and pyte builds compile the selected site, it is also what keeps those suites exercising the whole surface.

| Group | Flags | Notes |
|---|---|---|
| core | `CONSOLE` (on), `LOGGING` (on) | `LOGGING` off leaves `Logs::Logger` an inert shell - `_emit()` returns before formatting, so no call site and none of the ~19 headers holding a `Logger*` change |
| network | `NETWORK`, `NTP`, `MQTT` | `NTP`/`MQTT` require `NETWORK` |
| pages | `HOME_PAGE`, `SHELL_PAGE` (both = `CONSOLE`), `LOGS_PAGE`, `ALARMS_PAGE`, `PARAMS_PAGE`, `MONITOR_PAGE`, `TASKS_PAGE`, `NOTES_PAGE` | all require `CONSOLE`; `LOGS_PAGE` also requires `LOGGING`. **`SHELL_PAGE` gates the whole command interpreter** - `ShellPage` owns `Shell::CommandShell`. The boot page is whichever registers first, so it is positional, not `HOME`'s property |
| transports | `LOGS_PUBLISH`, `ALARMS_PUBLISH` (both need `LOGGING`+`MQTT`), `LOGS_MONITOR` (needs `LOGGING`) | `LOGS_MONITOR` is the secondary serial log sink - **not** `MONITOR_PAGE`, which is a console page |
| PLC | `PLC_SCAN` (on), `PLC_BLK_BASE`, `PLC_BLK_FILTERS` | `PLC_SCAN` off keeps `SiteDescriptor`'s program fields (removing trailing aggregate fields would break every site's initialiser) and does **not** remove control authority. `PLC_BLK_FILTERS` is `plc/blocks/filters.h`, distinct from `utils/filters.h` |
| field bus | `MODBUS_TCP_SERVER`, `MODBUS_TCP_CLIENT`, `MODBUS_RTU_CLIENT`, `MODBUS_RTU_SERVER` | all four implemented. The two SERVERs answer, the two CLIENTs ask: `RtuClient` is the field-bus master that scans the site's `XP_MODBUS` rows, `TcpSlave` serves the register map, `TcpMaster` is the sim's own self-check against that map, and `RtuSlave` is the device model the simulated field is built from. `TCP_*` need `NETWORK`; `RTU_CLIENT` and `TCP_SERVER` need `PLC_SCAN` |
| standalone | `SUN_TRACKER` | not instantiated by `BotanikFirmware`; a site owns the instance. Off in the header, on in `gh_mlb41/config.h` - otherwise nothing compiles `sun/` and its two test files contribute nothing to the gate |

A site's opt-in must yield to the command line, and `#ifndef` alone does not do that: with `-DBOTANIK_FEATURE_CONSOLE=OFF` the page flags are still undefined when the site config is read, so a bare `#define ..._PAGE 1` would turn the builder's choice into a dependency error the builder never wrote. Guard the block with `#if !defined(X) || X` ("unless explicitly turned off") - see `gh_mlb41/config.h`.

Gate with the preprocessor `#if BOTANIK_FEATURE_<X>` (it must drop member declarations and ctor-init entries; C++11 has no `if constexpr`). See `docs/integration/config.md`.

## Site-specific code (greenhouse targets)

`libbotanik` is generic and site-independent. Code specific to one physical greenhouse -
its variable table, its control programs, and the mock data animating them in the
simulator - is a **site**. The reference site is `gh_mlb41`; treat it plus
`BotanikFirmware` as canonical.

Three layers, generic to specific:

| Layer | Holds |
|---|---|
| `libbotanik` | generic machinery plus `BotanikFirmware`, which owns the generic components and drives the per-tick `loop()` |
| site (`gh_mlb41`) | the single source of truth for one greenhouse: its variable table, its programs, its shell commands, its simulated field |
| target | thin: selects one site and runs it (`botanik_sim`, `botanik_fw`) |

`GreenhouseMLB41 : BotanikFirmware` is a thin binding passing a `SiteDescriptor` to the
base constructor. The base owns every page and wires each table generically. `setup()`
and `loop()` are non-virtual; a site customizes through `register_pages()` (tab order,
page 0 = boot page), `register_commands()`, `register_telemetry()`, `on_setup()`, and the
six per-phase hooks (only `on_control()` is gated). The site is chosen at configure time
with `-DBOTANIK_SITE=<name>`.

The site's six files - `vars.h` (generated: every variable in one table, the calibration
blocks, the home fields, the RAM value store), `site.h`, `tasks.h` (the control programs),
`sim.h` (whole body `#ifdef TARGET_SIMULATOR`), `config.h`, `vars.cpp`.

### Rules

- **One variable table per site, shared by simulator and hardware.** Never fork it per
  target; evolve the table and its `simulate()` animator together, in the site.
- **The simulator is a generic harness**, calling `Site::simulate()` each beat into the
  same rows a Modbus driver fills on hardware. For rows on the bus, `FieldBus` carries
  them through a simulated `RtuSlave` over a socketpair, so the sim exercises the driver
  rather than standing in for it.
- **The site stays portable.** It must cross-compile for the OPTA, which does not define
  `TARGET_SIMULATOR`, so simulator-only code stays behind that guard.
- **Generic components address variables by NAME**, resolved once at bring-up into
  parallel `VarRef` arrays. A name matching no row resolves to the inert sink, and
  `tests/test_site_names.cpp` fails the gate if any does. **The one trap:** a `VarRef` in
  a `const` table is zero, and zero is a real row - never read a resolved index back out
  of the flash table it was declared in.
- **Config is one file per concern.** `libbotanik_config.h` is only the AGGREGATE; values
  live in `libbotanik/config/<concern>.h` and a module includes the one it reads.
  `config/base.h` is the preamble every concern header pulls first, and it reads the
  site's `config.h` BEFORE any default - that ordering is the whole contract. A constraint
  spanning two concerns is asserted at the consumer that makes it exist, not in either
  config header.
- **Config is site-overridable.** Every value defaults from an `#ifndef`-guarded
  `BOTANIK_<NAME>`; a site may `#define` it in `sites/<name>/config.h`, which CMake
  includes PUBLIC so the override is build-wide. A site pins a **feature flag** with an
  `#ifndef` guard, making precedence command line > site > header default.
- New site-level work goes in the site, not in `libbotanik` and not in a target's `main`.
