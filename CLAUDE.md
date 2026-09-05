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
`make` builds in parallel and a bare `ctest` runs cases concurrently once
[direnv](https://direnv.net) loads it. Without direnv, export `MAKEFLAGS` yourself or
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
make -C build check-full             # the sweep: clang-tidy, bare profile, generated-source drift, the OPTA cross build, then the gate under ASan+UBSan
make -C build check-bare             # just the bare profile: every optional feature off
make -C build coverage               # per-area line coverage of the library, both suites
make -C build botanik_tests          # just build the doctest binary
./build/tests/botanik_tests          # run it directly (doctest CLI)
./build/tests/botanik_tests -s       # ...every passing check, not just failures
./build/tests/botanik_tests -tc=<name>              # one test case
ctest --test-dir build --output-on-failure          # all CTest entries
ctest --test-dir build -R pyte --output-on-failure  # just the pyte suite
make -C build matiec                 # build the pinned matiec (needs flex/bison/autotools)
```

**`make check`** fails on the first problem: the library compiles `-Werror`, so building
the suite at all fails on a warning in a library source; then the doctest + pyte suites
via `ctest -LE unit`. The gate excludes the per-case entries because ONE PROCESS is what
catches a case corrupting state for the ones after it - which the per-case entries, each
in a fresh process, cannot see by construction.

That is the whole argument now. It used to be a speed one as well, back when the cases
were fast and the ~14 s of the per-case run was all process startup; the site scenarios
walk simulated hours, so the single process is the slower of the two and the gate pays
for the isolation it wants.

On a doctest failure the gate (`tests/gate.cmake`) re-runs the per-case entries itself, so
each failing case is named - paid only when something is broken. **If that re-run comes
back all-green the fault is usually INTERFERENCE between cases, not a flake**: reproduce it
with `./build/tests/botanik_tests`, the one-process configuration.

The exception is the socket-backed cases (`test_modbus_server.cpp`,
`test_modbus_master.cpp`), which bind a PID-derived 127/8 address so that parallel
processes do not fight over a port - a recycled PID can therefore meet the lingering
sockets of a run that just exited. **If ONLY those failed and nothing else did, re-run
before digging.**

**`make check-full`** runs clang-tidy over `src/` first, so a finding stops it before it
pays for an instrumented rebuild, then the whole `check` gate again in `build/asan/`. It
drives that tree's own `check` target, so the two cannot drift. Both halves are
instrumented, screen tests included.

It also runs `check-generated`, which regenerates into a scratch copy of the generator's
inputs and diffs against what is committed. The generated sources are committed and never
built, so nothing else holds them to the tables: edit a CSV, forget to regenerate, and
`make check` is green on a firmware without your row.

And it builds the OPTA (`check-opta`), which is the only thing that can catch a LINK
error: `Arduino.h` declares the whole wiring API and `libmbed.a` defines none of it, so a
HAL reaching for a call the build does not compile fails at link time on the cross build
alone, with `make check` green throughout. Skipped where `arm-none-eabi-g++` is absent.

The compiler that check is made against is PINNED: `vendor/matiec`, one upstream
commit, built by `make -C build matiec` into `build/matiec/iec2c`. Nothing pinned
it before, so two developers regenerating produced different `POUS.c` and the
diff above would have called whichever ran second wrong. `scripts/tables.py`
finds the pinned binary by itself; `IEC2C=` points it elsewhere. Building it
needs flex, bison and the autotools, and without them the `matiec` target is not
declared and the check falls back to whatever is on PATH.

**`make check-bare`** compiles `sites/bare` - every optional feature off - into
`build-bare/`, and a middle profile (console + shell + logs + alarms, no
variable model) into `build-bare-ui/`. Both are compile-only and build
`botanik_sim` rather than the suite, because the tests are written against a
site that HAS a variable table. The middle profile earns its place: the two
extremes only compile the FILE-level guards, and an `#if VARS` inside a page or
a shell command needs the console on and the table off.

It exists because an OFF path nothing builds rots silently: three had already
rotted when it was added. Part of `check-full`.

**`make coverage`** builds a third tree under `build/coverage/`, instrumented for
gcov, runs BOTH suites against it and prints per-area line coverage of the library,
writing `build/coverage.json` beside it. It REPORTS and never fails: a floor that
fails a build teaches people to lower the floor, and what this is for is noticing a
fall. Part of `check-full`; skipped where `gcov` is absent.

Both suites, because neither alone is the gate - the doctest binary never exercises
`network/`, and the pyte tests are the only thing that drives the shell over a PTY.
Two things it deliberately does not do, each of which gives a badly wrong answer:
it does not read `gcov -n`'s per-file summary (which charges a header's inline
bodies to whichever `.cpp` pulled them in - `health.cpp`, 223 lines, reports 13912
of them), and it does not count HEADERS at all (gcov gives a header's inline code
to ONE translation unit and reports every other as zero, which is a 34-point error
on the total). `.cpp` only, from the `-p` reports.

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

- `@brief` is 1-2 lines (hard cap 3); a whole doc block is <= 6 lines of PROSE
  (`@param`/`@return` are contract and do not count); at most one `@note`. No
  `@code` and no `@section` in a header - those go to `docs/dev/`.
- `@param`/`@return` only for a non-obvious unit, range, ownership or sentinel. Never
  to restate the parameter name.
- Implementations: non-obvious invariants, quirks and workarounds only. No banners, no
  comment restating the next line.
- Tests: the test name is the documentation. No header comment above a `TEST_CASE`.
- Prose: no "not X but Y" constructions, no history (`git log` has it), no em-dashes.

A header has **no prose ratio**: the per-entity caps above are the rule. A ratio there
measures how many things a header declares, not how wordy it is. Measured over
`libbotanik`, a documented entity costs 2.8 code lines and 1.9 prose lines, so any
header obeying "every public entity carries a `@brief`" lands near 90%; the old 50%
was reachable only by leaving half the API undocumented.

For a `.cpp` or a test the ratio does carry signal, because implementation prose is
discretionary: **prose lines <= 30% of code lines**, `@name`/`@{`/`@}` scaffolding not
counted. The tree sits at 27.9% and 21.5% (September 2026), so the bar is set just
above it to catch drift rather than to describe today. Over it, look for the two
things the rules above already forbid: a banner, and a comment restating the line
below.

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
| `hal/` | `platform.h` is the unified interface, in nested namespaces by concern: `Clock`, `Memory`, `Stack`, `Debug`, `System`, `Watchdog`, `DigitalIn`, `Pulse`, `Env`. `Serial` comes from the backend it includes; `Storage` and `Network` are their own headers (`storage.h`, `net.h`), being the two that need one. `simulator.h` / `opta.h` are the backends, each carrying only what must be INLINE; the cold half is `platform.cpp` plus one file per concern that outgrew it - `inputs.cpp`, `env.cpp`, `net.cpp`, `storage.cpp` | `porting.md` |
| `scheduler/` | `LoopScheduler`, the fixed-period loop driver: releases one tick per period, handles overruns, self-instruments per beat (CPU duty, jitter, work duration, stack high-water) into a `SchedulerContext`, which also carries the unit's `RunState` | `scheduler.md` |
| `tasks/` | The task table and `TaskTimer`. `LoopScheduler` decides WHEN the beat happens, `Tasks::Registry` decides WHAT runs in it. Sits BELOW `core/` | `tasks.md` |
| `core/` | The BOTTOM of the stack: `Component` (one hook per phase, `register_tasks()`) and `SystemContext` (a borrowed-pointer aggregate, constructor-injected into its consumers, NOT reachable through the console) | `architecture.md` |
| `firmware/` | `BotanikFirmware`, the Template-Method base owning every generic component and the non-overridable per-tick order. Sites customize only through the protected hooks | `architecture.md` |
| `vars/` | Gated by `BOTANIK_FEATURE_VARS`. The one model everything the firmware exposes goes through. A row is a `Kind` and a `VarType`; published, mapped, local, analog and writable are DERIVED. `Bank` holds the generated table and the RAM over it, `Var` (bank + index) is every row-level operation, and `Persist` is the durable half | `vars.md` |
| `plc/` | The POU vocabulary, in ST: `functions/*.st` has no state, `blocks/*.st` has state, and `runtime/` is the join between the beat and matiec's generated C. If it has to remember something it is a block. `functions/ieclib.txt` is what `iec2c -I` points at - OUR standard functions, so matiec's own GPL library is never loaded | `scan.md`, `st-toolchain.md` |
| `io/` | `LocalInputs`, the controller's own input terminals: every LOCAL input row found by walking the table at bring-up, published at `PHASE_INPUT` with `F_INVERT` folded in. The field-bus mirror image, so a program reads a terminal by name | `io.md` |
| `modbus/` | Four corners in one tree: `rtu/` is the RS-485 field bus, `tcp/` the plant network, each with both ends. `wire.h` and `response.h` are what they share | `modbus.md` |
| `console/` | Two layers: `AnsiTerminal` is the VT device owning a screen back-buffer, `Console` the multiplexed page display. `TextEditor` is the byte-efficient editing state machine | `screen.md`, `editor.md`, `input.md` |
| `pages/` | Full-screen views behind a common `Page` interface, all base-owned and site-independent | `pages.md` |
| `shell/` | `CommandShell`: commands are **noun then verb**, a noun parsing its own verb. Depends on the terminal device, not on `Console` or `pages/` | `pages.md` |
| `logs/` | `Logger` with a dual-bucket token-bucket rate limiter before its `LogWriter`s. `LOG_CRITICAL` is undroppable | `logging.md` |
| `alarms/` | `AlarmController` reads an `AlarmSourceTable`, never a bank. With `VARS` on that is `Vars::BankAlarms` over the site's `K_ALARM` rows, each carrying both its boolean and its dwell policy; without it, `StaticAlarmTable` over a flash array. Not feature-gated: every build has alarms | `health.md`, `blind-spots.md` |
| `health/` | `HealthMonitor` owns the `AlarmController`, raises the platform-health alarms from scheduler metrics, runs the POST, and produces the watchdog verdict | `health.md` |
| `network/` | `NetworkHandler` / `NameResolver` / `NtpClient` / `MqttClient`, all non-blocking. The sim backend is hermetic - it emulates DNS/NTP/MQTT in-process | `network.md` |
| `sun/` | `SunPositionTracker` over the NOAA solar-position math. `BotanikFirmware` owns the instance, registers it and binds its `geo_` parameters; a program reads it through `SetupContext::sun` | - |
| `utils/` | `BufferedSerial` (256-byte RX ring, 4096-byte TX; not a Component and pumped by whoever owns the wire), metric filters, uptime, text helpers, the firmware assert | `concurrency.md` |

### Rules that are not in a header

- **Control authority** is decided by two INDEPENDENT sources and the STRICTER wins:
  the OPERATOR's standing choice (`sys_control` for all of them, `programs disable` for
  one) and the SYSTEM's hold (`Scheduler::RunState`, which is only RUN between
  `sys_startup_sec` and the shutdown). `Plc::Runtime::Plant::running()` is where the two
  meet. A program that is not running does not FREEZE what it drives: its rows drop to
  their declared safe state, which is what `Descriptor::def` means for an output. The
  roster of rows a program authors is generated from what matiec says its body WRITES
  (`__SET_EXTERNAL` in `POUS.c`), because an ST row resolves VAR_IN_OUT and so claims no
  exclusive writer - the bank cannot be asked whose a row is.
- **The unit has a lifecycle**, one `Scheduler::RunState` in the `SchedulerContext`:
  SETUP inside `setup()`, START while `sys_startup_sec` runs out, RUN in service, STOP
  while `sys_shutdown_sec` runs out, HALT waiting for the ending `HealthMonitor::Stop`
  names. The FIRMWARE advances it; the scheduler only holds it. Nothing runs a law
  outside RUN, and the first MQTT sweep waits for it too - metrics published before the
  field answers are declared values dressed as readings, and the broker RETAINS them.
- **The scan cycle has no driver.** `Plc::Runtime::Plant` registers one
  `Tasks::Registry` task per ST program, PHASE_CONTROL, at the rate
  `tables/programs.csv` gave it. A program due late runs ONCE - missed activations are
  dropped, never caught up. There is no `dt_ms` and no `first_scan`: a program takes no
  argument, so a block that needs elapsed time reads `sys_now_ms` and differences it
  itself, and one that needs "have I run before" seeds on its first call. See `scan.md`.
- **Library rows.** The library declares rows of its own in
  `vars/tables/library.csv`, and the GENERATOR merges them into the site's table -
  one array, one index space, nothing registered at runtime. The library's
  parameters come first, so the contiguous `K_PARAM` run holds. Registers 900-999
  are the library's. **Whatever the library reads by name, the library declares**,
  and it reads it through a generated `Vars::Row::` constant. A site tunes one by
  REDECLARING it: the generator replaces the library's row IN PLACE and keeps its
  register, so the whole row is the site's and no `BOTANIK_*` macro is needed per
  overridable field. See `vars.md`.
- **Analog calibration.** An `AI`/`AO` row points at a shared `const AnalogCal` of six
  `Tunable`s; `scale`/`offset` are never stored but derived, so a recalibration lands the
  moment the parameter commits. Anything DISPLAYING a band goes through
  `Var::band()`, which resolves it. See `vars.md`.
- **A band may name another row.** A pair with a required relationship - a target's
  floor and ceiling, a ramp's start and end - is only wrong AS A PAIR, and a fixed
  band cannot see that: each value alone is legal. `params.csv` and `modbus_in.csv`
  carry a `lo_row` / `hi_row` beside the numbers, the generator resolves them to
  indices, and `param_check()` judges an edit against `Var::band()` - so the limit
  is whatever the other row holds NOW. Constrain BOTH ends against each other; the
  pair stays reachable as long as it starts valid, which `tables.py` refuses to
  generate without. Anything READING a band goes through `Var::band()`; a band whose
  two ends are equal and fixed is NO band.
- **Some things the plant cannot see are DECISIONS**, not gaps: an actuator
  reports no fault because no fault contact is wired, and `cli_phase_qlt` says
  where a phase came from rather than whether it is legal. `docs/dev/blind-spots.md`
  is the list, and the place to add to when the answer to "why is there no check
  for X" is "we chose not to". A review with no such list re-raises every one.
- **Program health.** A program may opt in with `health` in `programs.csv` and
  get `prog_<name>_health`, a `T_ENUM` row over `Plc::Health` -
  `HEALTHY`, `DEGRADED` (it answered, on evidence it does not fully trust),
  `FAULTED` (it could not answer; what it wrote is its safe state). The LAW
  writes it, and opting in obliges the body to write it on every path, which
  `tables.py plant` checks off matiec's write list the same way it checks a
  quality. `FAULTED` is the declared value, so it is what the row reads before
  the first scan and where it goes when the program leaves it. `Plant` only
  reads it back for the TASKS page; a program that did not opt in has no verdict
  and shows blank. The ST type is `plc/functions/health.st`, generated onto the
  C++ enum by `health.gen.h` exactly as `QUALITY` is. See `scan.md`.
- **Parking.** A program may opt in with `park` in `programs.csv` and get
  `prog_<name>_park` (Plant writes, the law reads) and `prog_<name>_parked` (the
  law writes). Such a program is NOT dropped when it leaves the scan: `park` goes
  true, its body keeps being called while it travels, and its rows go to their
  declared values only once it answers - so a roof shuts before the unit halts
  rather than reading 0.25 while sitting open. `Plant::parking()` holds the
  lifecycle in STOP; `plc_parkTimeout_sec` caps it, because the way out may not
  depend on an actuator. `tables.py plant` refuses an opt-in that never writes its
  `_parked` row. Not for an actuator whose safe state is where it already is - the
  thermal screen. See `scan.md`.
- **Quality is a ROW.** `x_qlt` carries `x`'s `Vars::Quality` and is the only
  place a quality is stored: the generator emits the companion IMMEDIATELY AFTER
  its row and `F_QLT` marks the pair, `Var::valid()`
  reads that row, a driver's `publish()` writes it, and ST reads and writes it like
  any other. There is nothing to keep in step. A row with no companion is valid,
  always, and so is every companion. Which rows get one is GENERATED - one per
  input, one per row a program that opted in (`quality` in `programs.csv`) writes,
  never an alarm - and opting in obliges the law to write every one of them, which
  `tables.py plant` checks off matiec's write list. `Ref::get()` substitutes the
  declared value for a law unless the quality is USABLE - `Q_GOOD`, or `Q_HELD`,
  which is a law's own fallback that readers should compute on though nothing
  measured it; `Var::value()` never substitutes, so a page shows the reading that
  was implausible. Three codes, and the four that went were diagnoses with homes
  of their own. See `quality-companions.md`.
- **Naming files a variable.** `var_group()` is the prefix of a name up to the first `_`;
  `published()` is true iff a row is neither a parameter, an alarm, nor a quality.
  There is no group table to add a variable to and none to forget.

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

**Defaults are a minimal core, opt in**: a build that sets nothing gets `CONSOLE` + `HOME_PAGE` + `SHELL_PAGE` + `LOGGING` + `VARS` + `PLC`. Everything else is off. `src/sites/gh_mlb41/config.h` is the worked full profile, and because the test and pyte builds compile the selected site, it is also what keeps those suites exercising the whole surface.

`src/sites/bare/config.h` is the OTHER end: every optional flag off, nothing declared, existing to be COMPILED rather than run. `make check-bare` builds it and a middle profile (console + shell, no variable model), because an OFF path nothing builds rots - three had already rotted when it was added.

| Group | Flags | Notes |
|---|---|---|
| core | `CONSOLE` (on), `LOGGING` (on) | `LOGGING` off leaves `Logs::Logger` an inert shell - `_emit()` returns before formatting, so no call site and none of the ~19 headers holding a `Logger*` change |
| network | `NETWORK`, `NTP`, `MQTT` | `NTP`/`MQTT` require `NETWORK` |
| pages | `HOME_PAGE`, `SHELL_PAGE` (both = `CONSOLE`), `LOGS_PAGE`, `ALARMS_PAGE`, `PARAMS_PAGE`, `MONITOR_PAGE`, `TASKS_PAGE`, `SYSTEM_PAGE`, `NOTES_PAGE` | all require `CONSOLE`; `LOGS_PAGE` also requires `LOGGING`. **`SHELL_PAGE` gates the whole command interpreter** - `ShellPage` owns `Shell::CommandShell`. The boot page is whichever registers first, so it is positional, not `HOME`'s property |
| transports | `LOGS_PUBLISH`, `ALARMS_PUBLISH` (both need `LOGGING`+`MQTT`), `LOGS_MONITOR` (needs `LOGGING`) | `LOGS_MONITOR` is the secondary serial log sink - **not** `MONITOR_PAGE`, which is a console page |
| PLC | `PLC` (on) | `PLC` off keeps `SiteDescriptor`'s program fields (removing trailing aggregate fields would break every site's initialiser) and does **not** remove control authority. The POU vocabulary is ST now, compiled into the site, so there is nothing per-block to gate |
| field bus | `MODBUS_TCP_SERVER`, `MODBUS_TCP_CLIENT`, `MODBUS_RTU_CLIENT`, `MODBUS_RTU_SERVER` | all four implemented. The two SERVERs answer, the two CLIENTs ask: `RtuClient` is the field-bus master that scans the site's field rows (an INPUT or OUTPUT on a device other than 0), `TcpSlave` serves the register map, `TcpMaster` is the sim's own self-check against that map, and `RtuSlave` is the device model the simulated field is built from. `TCP_*` need `NETWORK`; `RTU_CLIENT` and `TCP_SERVER` need `VARS` |
| standalone | `SUN_TRACKER` | owned by `BotanikFirmware` and reached through `SetupContext::sun`, so no site declares one. Off in the header, on in `gh_mlb41/config.h` - otherwise nothing compiles `sun/` and its two test files contribute nothing to the gate |

A site's opt-in must yield to the command line, and `#ifndef` alone does not do that: with `-DBOTANIK_FEATURE_CONSOLE=OFF` the page flags are still undefined when the site config is read, so a bare `#define ..._PAGE 1` would turn the builder's choice into a dependency error the builder never wrote. Guard the block with `#if !defined(X) || X` ("unless explicitly turned off") - see `gh_mlb41/config.h`.

Gate with the preprocessor `#if BOTANIK_FEATURE_<X>` (it must drop member declarations and ctor-init entries; C++11 has no `if constexpr`). See `docs/integration/config.md`.

**Four rules, in order of preference.** They exist because a guard is not free:
every one is a line a reader steps over, and a wrongly placed one compiles fine
in the profile you happen to build.

1. **A `.cpp` guards its whole body**, with the flag its header uses, and the
   `#endif` is the LAST line of the file. Anything after it is compiled against
   a class the header just gated away. Two files got this wrong and neither had
   ever built with its flag off; `check-bare` is what catches it now.
2. **Prefer an inert shell to gating a declaration.** `Logs::Logger` with
   `LOGGING` off keeps its type and guts `_emit()`: 3 guards inside `logger.cpp`,
   and none of the ~19 headers holding a `Logger*` change. Declaration gating
   propagates instead - the member, its ctor-init entry, its accessor, and every
   call site.
3. **Never gate a borrowed pointer or an aggregate field.** A forward-declared
   pointer costs nothing to declare, so the `#if` only moves to each consumer -
   which needs one anyway to dereference it. Gate the CODE, not the field.
   `SiteDescriptor`'s tables and every `SystemContext` pointer follow this.
4. **A site pins a flag with `#if !defined(X) || X`**, so the command line still
   wins: precedence is command line > site > header default.

`firmware/` holds about half of all the guards in the library, and that is the
composition root doing its job - it is the one place that knows which optional
subsystems exist. Concentration there is the goal; a guard appearing in a
subsystem that could have used rule 2 or 3 is the smell.

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
and `loop()` are non-virtual; a site customizes through `register_vars()`,
`register_pages()` (tab order, page 0 = boot page), `register_commands()`,
`register_telemetry()` and `on_setup()`, where its control programs and any
components of its own register. There is no per-phase hook on the firmware: work
at a point in the beat is a `Component` or an ST program. The site is chosen at configure time
with `-DBOTANIK_SITE=<name>`.

A site is DATA plus ST. Its rows, its enums, its home screen, its field devices
and its `SiteDescriptor` are generated from `tables/*.csv` and `site.toml` by
`scripts/tables.py`; its LAWS are the ST under `programs/`, compiled by matiec.
Its enumerated codes are generated on BOTH sides from `tables/enums.csv` - a C++
enum and an ST one that are the SAME type - so a law compares a phase against
`PHASE_DAY` rather than against 3.
The library declares its own rows the SAME way, in
`src/libbotanik/vars/tables/library.csv` - one table rather than one per kind,
because the rows interleave and the `K_PARAM` run has to stay contiguous, and
with a `guard` column for the feature flags a row needs. The generator MERGES
those rows into the site's and emits ONE table: nothing is registered at
runtime, and the site states no count.

A site OVERRIDES a library row by redeclaring the name: the generator replaces
the library's row in place and keeps its register - `geo_lat_deg` is the worked
example.

A control program is ONE file, `programs/<stage>/<name>/<name>.st`, plus a row
in `tables/programs.csv` giving its instance, source and rate. No header, no
class, no registration: it names the rows it uses in `VAR_EXTERNAL CONSTANT`
(read) and `VAR_EXTERNAL` (written), and the body is the law. The site's own
function blocks are one file each, `blocks/<name>/<name>.st`.

Its RS-485 field devices are `tables/modbus_devices.csv` and
`tables/modbus_models.csv`.

**The generated files are committed and never built.** Run
`scripts/tables.py generate|config|plant|points [<site>]` after editing a table or
an ST file, and commit what comes out - `make check-generated` is what catches you
if you forget.

**The generator owns the checks.** It is the one thing that sees the whole table,
so everything the site test used to assert about it is refused at generate time
instead: names unique under case folding, registers unique and inside their
block, a mapped kind with no register, local terminals unique per direction, a
band end that names no row or the wrong type, a default outside its own band, an
alarm code too long for the page. Nothing it checks is checked again at runtime.

`plant` is the one that runs matiec, and it checks what only the compiler's own
answer can: a row declared writable that the body never writes, an output, alarm
or status row no program writes at all, a quality an opted-in program owes and
does not write, and a parking program that never answers - all read off matiec's
`__SET_EXTERNAL` calls. A program naming a row no table declares fails to
COMPILE, because a binding is a generated index.

What is left for the suite is BEHAVIOUR: `tests/test_site.cpp` runs the whole
site and checks that nothing moved a parameter it does not own.

### Rules

- **One variable table per site, shared by simulator and hardware.** Never fork it per
  target; evolve the table and its `simulate()` animator together, in the site.
- **The simulator is a generic harness**, calling `Site::simulate()` each beat into the
  same rows a Modbus driver fills on hardware. For rows on the bus, `FieldBus` carries
  them through a simulated `RtuSlave` over a socketpair, so the sim exercises the driver
  rather than standing in for it.
- **The site stays portable.** It must cross-compile for the OPTA, which does not define
  `TARGET_SIMULATOR`, so simulator-only code stays behind that guard.
- **Generic components address variables by INDEX**, through the generated
  `Vars::Row::<name>` constants (`generated/rows.gen.h`, reached by the library
  through `BOTANIK_SITE_ROWS`). A name no table declares is a COMPILE error.
  `Bank::find()` stays for the console and for a log sink naming its own level row.
  **The one trap:** an index is a bare `uint16_t` and zero is a real row - never
  read one back out of a `const` table it was declared in; `VAR_NONE` is 0xFFFF for
  that reason.
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
