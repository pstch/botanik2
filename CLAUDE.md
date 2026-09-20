# CLAUDE.md

Guidance for Claude Code (claude.ai/code) when working in this repository.

## Project overview

Botanik is a greenhouse control system for an Arduino OPTA PLC (STM32H747
dual-core ARM), running on the M7 core. It manages irrigation, ventilation,
cooling and monitoring. The core is `libbotanik`, a C++11 library that builds
either as a Linux simulator (`SIMULATOR`) or for the hardware (`OPTA`).

## Build

CMake 3.31+ and a C++11 compiler. Default target is `SIMULATOR`.

```bash
cmake -B build -DBOTANIK_TARGET=SIMULATOR   # configure
make -C build                               # build
make -C build botanik_sim                   # one target
cmake -B build -DBOTANIK_TARGET=OPTA        # cross-compile
```

OPTA builds use `arm-none-eabi-gcc/g++` and the Arduino Mbed OPTA core
(`arduino-cli core install arduino:mbed_opta`). CMake takes the highest version
under `~/.arduino15`; override with `-DMBED_CORE_ROOT=...` or `ARDUINO_DATA_DIR`.

`.envrc` exports `MAKEFLAGS=-j4` and `CTEST_PARALLEL_LEVEL` for
[direnv](https://direnv.net). `ccache` is used when installed.

Builds default to `DEBUG` (`-O0 -g3`). `-DCMAKE_BUILD_TYPE=RELEASE` gives `-O3`,
LTO, `NDEBUG` and a stripped image. Sanitizers default off on every build type;
`-DBOTANIK_SANITIZE=address+undefined` instruments a whole tree, and
`make check-full` builds its own instrumented tree under `build/asan/`.

Warnings are errors on the library's own sources (`BOTANIK_WERROR`, default ON,
PRIVATE to `botanik`, so the sketch and the suite keep `-Wall -Wextra` without
`-Werror`). `scheduler.cpp` adds `-Wdouble-promotion` to keep the per-beat metric
math single-precision; the M4 has no double FPU.

## Tests and checks

Host-only, SIMULATOR build. Everything runs under botanik's own CTest:

- **doctest** in `tests/` (vendored `tests/vendor/doctest.h`), registered twice:
  one unlabelled entry running the whole binary, and one entry per `TEST_CASE`,
  labelled `unit;unit:doctest`, so `ctest -R` or an IDE can address a single case.
- **pyte** in `tests/pyte/`, driving `botanik_sim` over a PTY and asserting its
  rendered ANSI, through the harness in `tests/kit/pyte/`. Registered only when
  `pytest` and `pyte` import, through `botanik_add_pyte_tests()` (cmake/testkit.cmake). Regenerate goldens with
  `make -C build pytest-update` and review the diff.
- **examples**: every `examples/<name>/` is configured and built as its own
  consumer project in `build/examples/<name>/` (cmake/examples.cmake). Each gets
  one unlabelled `example:<name>` entry running that project's own ctest, and one
  `example:<name>:<case>` entry per case of its `<name>_tests` binary, labelled
  `unit;unit:<name>`. Before `make examples` has run, a failing
  `example:<name>:NOT_BUILT` stands in. Every example builds against this
  checkout: ref and nosite link `extern/botanik` to it, and gh_mlb41's
  CMakeLists uses the enclosing botanik when it sits in one's `examples/`.
  gh_mlb41 is a submodule with `update = none`, since it pins botanik in turn and
  a recursive clone would nest forever: `git submodule update --init --checkout
  examples/gh_mlb41` populates it, and without it the gate skips it.

Nothing in `tests/` names a site's rows: the library's cases use library rows,
so they pass on any full-profile site. What a site's table decides is tested
beside that site (`examples/ref/tests/`, its `pyte/` included).

```bash
make -C build check                  # the pre-push gate (cross-compile included)
make -C build check-full             # the sweep
make -C build check-alloff           # the library with every optional feature off
make -C build examples               # build every example (check does it too)
make -C build coverage               # per-area line coverage, both suites
make -C build botanik_tests          # just build the doctest binary
./build/tests/botanik_tests          # run it directly (doctest CLI)
./build/tests/botanik_tests -tc=<name>              # one case
ctest --test-dir build --output-on-failure          # all CTest entries
ctest --test-dir build -R pyte --output-on-failure  # just pyte
make -C build codegen                # regenerate whatever no longer matches its inputs
make -C build check-opta-lib         # just the cross compile, no link
make -C build matiec                 # build the pinned matiec into vendor/matiec/build/
make -C build docs                   # codegen, then render the manual
make -C build docs-screens           # recapture the console screenshots
```

`make docs` runs codegen first, so nothing it documents is stale, then renders
the manual, in a few seconds. A site repository's `make docs` (cmake/consumer.cmake)
renders its own `docs/` and `src/` with botanik's pages and sources in the same
run, through botanik's Doxyfile with the paths re-pointed, into `build/docs/html`. `docs-screens` is deliberately **not** one of its dependencies: it
boots a simulator per scenario and takes a minute and a half, too much to pay
for every render.

`scripts/screens.py` is what that target runs. It drives `botanik_sim` through
the pyte harness and writes one rendered 80x24 screen per console page into
`docs/generated/screens/`, which the user manual pulls in with `@verbinclude`.
Generated documentation lives in `docs/generated/`. It writes `.txt` only, so it
can never touch a page's prose, and it masks the git hash and the measured TPS -
the two cells that would otherwise rewrite a committed screen on every commit.

The simulated days (`BOTANIK_SIM_SCENARIO`) are a site's, not the library's:
gh_mlb41 carries `scripts/scenarios.tsv`, `scripts/sim_scenario.sh` and a
`scenario_screens.py` that imports `screens.py` for its own screens.

**`make check`** fails on the first problem: the library compiles `-Werror`, so
a warning in a library source fails the build of the suite; it runs
`check-generated` (regenerate every site into a scratch copy and diff it both
ways, about a second - the stamp hashes the generator's INPUTS, so a generated
file edited by hand is the one thing this tree can get wrong silently), it
cross-compiles the library and the site through `check-opta-lib` (seconds, against the
persistent `build-opta/`, because a compile error is not a cross-build subtlety:
newlib against glibc, 32-bit `long`, a different `<algorithm>`), builds every
example, then runs `ctest -LE unit`: the whole doctest binary, each
`example:<name>` and the pyte files. Whole binaries run in ONE process, which is
what catches a case corrupting state for the ones after it.

doctest names the failing case, with file and line, in that one-process run. For
each whole binary that failed (read from CTest's `LastTestsFailed.log`),
`tests/gate.cmake` re-runs that binary's per-case entries (`-L unit:<name>`),
each in a fresh process, which answers a different question: an all-green
re-run means the case fails only in company, so the fault is interference.
Reproduce it with the binary itself, e.g. `./build/tests/botanik_tests`. The
exception is the socket-backed cases (`test_modbus_server.cpp`,
`test_modbus_master.cpp`), which bind a PID-derived 127/8 address, so a recycled
PID can meet the lingering sockets of a run that just exited. If only those
failed, re-run before digging.

**`make check-full`** runs clang-tidy over `src/` first, then `check-generated`
(regenerate every example into a scratch copy and diff it file by file against
its committed `generated/`, both ways - it is in `check` too), `check-alloff`, `check-opta` (the cross LINK, the only thing that
catches a link error: `Arduino.h` declares the whole wiring API and `libmbed.a`
defines none of it; the cross COMPILE is in `check` already), `coverage`, and finally the whole gate again under
ASan+UBSan in `build/asan/`. It drives that tree's own `check` target, so the two
cannot drift. Halves are skipped where their tool is absent.

The compiler is pinned: `vendor/matiec`, one upstream commit, built on first
use into `vendor/matiec/build/iec2c` (gitignored, once per checkout, shared by
every build tree). The generator builds it itself, and only for a site with
laws; `IEC2C=` points it at another compiler instead.

**`make check-alloff`** compiles the library with every optional feature off and
no site, the only build that compiles the file-level guards. `examples/nosite`
is the other no-site build: the console, its shell and the task view reused as
components, which keeps "a component works without a variable table" true.

**`make coverage`** builds a third tree under `build/coverage/`, runs both suites
and prints per-area line coverage of the library, writing `build/coverage.json`.
It reports and never fails. Both suites, because the doctest binary never
exercises `network/` and the pyte tests are the only thing driving the shell over
a PTY. It counts `.cpp` only, from the `-p` per-source reports: `gcov -n`'s
per-file summary charges a header's inline bodies to whichever `.cpp` pulled them
in, and headers are attributed to one TU and reported as zero in every other.

**clang-tidy is not in `check`** (it re-lints on any touched header), but it is
the first half of `check-full`.

```bash
make -C build clang-tidy       # incremental: one stamp per TU under build/clang-tidy/
rm -rf build/clang-tidy && make -C build clang-tidy   # the whole tree
```

It runs the `bugprone-*` checks from `.clang-tidy` with findings promoted to
errors, and exists only when clang-tidy was found at configure time and the
generator is Make (the stamps' header tracking is the Makefile generator's own
dependency scanner). Re-configuring, or editing `.clang-tidy`, invalidates every
stamp.

The pyte suite drives `botanik_sim`, which a plain `make` builds. CTest never
builds anything and CMake's `test` target cannot be given dependencies, so a
screen test that finds the sim missing fails loudly rather than skipping.

## Comments and prose

Full rules in `CONTRIBUTING.md` § Documentation. The governing one:

> Comments say what the code cannot: units, ownership, sentinels, wire facts,
> hardware quirks, invariants. Rationale, alternatives, history and architecture
> go to `docs/`, or are deleted.

A comment that would still be true after the function was rewritten a different
way is rationale and does not belong next to the code.

- `@brief` is 1-2 lines (hard cap 3); a doc block is <= 6 lines of prose
  (`@param`/`@return` are contract and do not count); at most one `@note`. No
  `@code` and no `@section` in a header.
- `@param`/`@return` only for a non-obvious unit, range, ownership or sentinel.
- Implementations: non-obvious invariants, quirks and workarounds only. No
  banners, no comment restating the next line.
- Tests: the test name is the documentation. No header comment above a `TEST_CASE`.
- No "not X but Y" constructions, no history, no em-dashes.

A header has no prose ratio; the per-entity caps above are the rule. For a `.cpp`
or a test, keep prose to 30% of code lines or under.

## Architecture

**Everything is non-blocking.** Every stateful object exposes a `step()`/`loop()`
doing a small bounded amount of work and returning immediately; the main loop
calls them in a fixed order. Blocking stalls the RTOS and trips the watchdog.

**Code reaches the platform only through HAL**, never raw `<time.h>` or platform
APIs. `TARGET_OPTA` appears only under `hal/`; shared code never branches on it
(`TARGET_SIMULATOR` may, for dev-only scaffolding).

Subsystems under `src/libbotanik/`; the header is the API.

| Directory | What it is |
|---|---|
| `hal/` | `platform.h` is the unified interface, in nested namespaces by concern: `Clock`, `Memory`, `Stack`, `Debug`, `System`, `Watchdog`, `DigitalIn`, `Pulse`, `Env`. `Serial` comes from the backend; `Storage` and `Network` have headers of their own. `simulator.h` / `opta.h` are the backends, carrying only what must be inline; the cold half is `platform_opta.cpp` / `platform_sim.cpp` plus one file per concern that outgrew it, each guarding its whole body on its own target |
| `scheduler/` | `LoopScheduler`, the fixed-period loop driver: releases one tick per period, handles overruns, self-instruments per beat (CPU duty, jitter, work duration, stack high-water) into a `SchedulerContext`, which also carries the unit's `RunState` |
| `tasks/` | The task table and `TaskTimer`. `LoopScheduler` decides when a beat happens, `Tasks::Registry` what runs in it. Sits below `core/` |
| `core/` | The bottom of the stack: `Component` (one hook per phase, `register_tasks()`) and `SystemContext` (a borrowed-pointer aggregate, constructor-injected, not reachable through the console) |
| `firmware/` | `BotanikFirmware`, the Template-Method base owning every generic component and the non-overridable per-tick order. Sites customize through the protected hooks |
| `vars/` | The one model everything the firmware exposes goes through. A row is a `Kind` and a `VarType`; published, mapped, local, analog and writable are derived. `Bank` holds the generated table and the RAM over it, `Var` (bank + index) is every row-level operation, `Persist` the durable half |
| `plc/` | The POU vocabulary, in ST: `functions/*.st` has no state, `blocks/*.st` has state, `runtime/` joins the beat to matiec's generated C. If it has to remember something it is a block. `functions/ieclib.txt` is what `iec2c -I` points at, so matiec's own GPL library is never loaded. `Plc::Health` is NOT here: it is library-wide vocabulary, generated into `core/health.h` |
| `io/` | `LocalInputs`, the controller's own input terminals: every local input row found by walking the table at bring-up, published at `PHASE_INPUT` with `F_INVERT` folded in |
| `modbus/` | Four corners in one tree: `rtu/` is the RS-485 field bus, `tcp/` the plant network, each with both ends. `wire.h` and `response.h` are shared |
| `console/` | `AnsiTerminal` is the VT device owning a screen back-buffer, `Console` the multiplexed page display, `TextEditor` the byte-efficient editing state machine |
| `pages/` | Full-screen views implementing `Terminal::Page`, all base-owned and site-independent. The interface itself is `console/page.h`, beside the Console that dereferences it |
| `shell/` | `CommandShell`: commands are noun then verb, a noun parsing its own verb. Depends on the terminal device, not on `Console` or `pages/` |
| `logs/` | `Logger` with a dual-bucket token-bucket rate limiter before its `LogWriter`s. `LOG_CRITICAL` is undroppable |
| `alarms/` | `AlarmController` reads an `AlarmSourceTable`, never a bank: that is what keeps this tree free of `vars/`, and what lets a test stand its own source in front of the controller. The one implementation is `Vars::BankAlarms` over the site's `K_ALARM` rows, each carrying its boolean and its dwell policy |
| `health/` | `HealthMonitor` owns the `AlarmController`, raises the platform-health alarms from scheduler metrics, runs the POST, and produces the watchdog verdict |
| `network/` | `NetworkHandler` / `NameResolver` / `NtpClient` / `MqttClient`, all non-blocking. The sim backend is hermetic: it emulates DNS/NTP/MQTT in-process |
| `sun/` | `SunPositionTracker` over the NOAA solar-position math. `BotanikFirmware` owns the instance and binds its `geo_` parameters; a program reads it through `SetupContext::sun` |
| `utils/` | `BufferedSerial` (256-byte RX ring, 4096-byte TX; not a Component, pumped by whoever owns the wire), metric filters, uptime, text helpers, the firmware assert |

### Rules that are not in a header

- **Control authority** is decided by two independent sources and the stricter
  wins: the operator's standing choice (`sys_control`, or `programs disable` for
  one) and the system's hold (`Scheduler::RunState`, RUN only between
  `sys_startup_sec` and the shutdown). `Plc::Runtime::Plant::running()` is where
  they meet. A program that is not running does not freeze what it drives: its
  rows drop to their declared safe state, which is what `Descriptor::def` means
  for an output. The roster of rows a program authors is generated from what
  matiec says its body writes (`__SET_EXTERNAL` in `POUS.c`), because an ST row
  resolves VAR_IN_OUT and claims no exclusive writer.
- **The unit has a lifecycle**, one `Scheduler::RunState` in the
  `SchedulerContext`: SETUP inside `setup()`, START while `sys_startup_sec` runs
  out, RUN in service, STOP while `sys_shutdown_sec` runs out, HALT waiting for
  the ending `HealthMonitor::Stop` names. The firmware advances it; the scheduler
  only holds it. Nothing runs a law outside RUN, and the first MQTT sweep waits
  for it too: metrics published before the field answers are declared values
  dressed as readings, and the broker retains them.
- **The scan cycle has no driver.** `Plc::Runtime::Plant` registers one
  `Tasks::Registry` task per ST program, PHASE_CONTROL, at the rate
  `tables/programs.csv` gave it. A program due late runs once; missed activations
  are dropped, never caught up. There is no `dt_ms` and no `first_scan`: a
  program takes no argument, so a block needing elapsed time reads `sys_now_ms`
  and differences it, and one needing "have I run before" seeds on its first call.
- **Library rows.** The library declares rows of its own in
  `libbotanik/tables/rows.csv` and the generator merges them into the site's table:
  one array, one index space, nothing registered at runtime. The library's
  parameters come first, so the contiguous `K_PARAM` run holds. Registers 900-999
  are the library's. Whatever the library reads by name, the library declares,
  through a generated `Vars::Row::` constant. A site tunes one by redeclaring it:
  the generator replaces the row in place and keeps its register.
- **Analog calibration.** An `AI`/`AO` row points at a shared `const AnalogCal`
  of six ends - two raw, two engineering, two plausibility - each a fixed
  number OR the name of another row. The values and the rows are parallel
  arrays indexed by `CalEnd`, so the two cannot drift apart by position; named
  pairs would pad to 16 bytes each and cost half again in flash. `scale` and
  `offset` are derived rather than stored, so a recalibration lands the moment
  the parameter commits. Anything displaying a band goes through
  `Var::band()`.
- **A band may name another row.** A pair with a required relationship (a
  target's floor and ceiling, a ramp's start and end) is only wrong as a pair,
  and a fixed band cannot see that. `params.csv` and `modbus_in.csv` carry
  `lo_row` / `hi_row` beside the numbers, the generator resolves them to indices,
  and `param_check()` judges an edit against `Var::band()`, so the limit is
  whatever the other row holds now. Constrain both ends against each other; the
  pair stays reachable as long as it starts valid, which `tables.py` refuses to
  generate without. A band whose two ends are equal and fixed is no band.
- **Some things the plant cannot see are decisions**, not gaps: an actuator
  reports no fault because no fault contact is wired, and `cli_phase_qlt` says
  where a phase came from rather than whether it is legal. Add to the blind-spot
  list when the answer to "why is there no check for X" is "we chose not to".
- **Program health.** A program may opt in with `health` in `programs.csv` and
  get `prog_<name>_health`, a `T_ENUM` row over `Plc::Health`: `HEALTHY`,
  `DEGRADED` (it answered, on evidence it does not fully trust), `FAULTED` (it
  could not answer; what it wrote is its safe state). The law writes it, on every
  path, which `tables.py plant` checks off matiec's write list. `FAULTED` is the
  declared value, so it is what the row reads before the first scan. `Plant` only
  reads it back for the TASKS page. The ST type is the `TYPE` block in
  `plc/functions/health.st`, which the generator CHECKS against
  `libbotanik/tables/enums.csv` - the one source both the C++ enum and the constants
  matiec is handed come from, as `QUALITY` does.
- **The program gate is a row.** Every entry in `programs.csv` gets
  `prog_<name>_run`, a `T_BOOL` parameter defaulting on, generated into the
  `K_PARAM` run. `Plant::enabled()` reads it and `programs start|stop` writes
  it; `programs enable|disable` is the same write plus the save, which is the
  whole of what makes it durable. No opt-in: the gate is Plant's, not the law's,
  and a unit that comes up with a law out of the scan says which on the PARAMS
  page.
- **Parking.** A program may opt in with `park` in `programs.csv` and get
  `prog_<name>_park` (Plant writes, the law reads) and `prog_<name>_parked` (the
  law writes). Such a program is not dropped when it leaves the scan: `park` goes
  true, its body keeps being called while it travels, and its rows go to their
  declared values only once it answers, so a roof shuts before the unit halts.
  `Plant::parking()` holds the lifecycle in STOP; `plc_parkTimeout_sec` caps it,
  because the way out may not depend on an actuator. `tables.py plant` refuses an
  opt-in that never writes its `_parked` row. Not for an actuator whose safe
  state is where it already is.
- **Quality is a row.** `x_qlt` carries `x`'s `Vars::Quality` and is the only
  place a quality is stored: the generator emits the companion immediately after
  its row, `F_QLT` marks the pair, `Var::valid()` reads that row, a driver's
  `publish()` writes it, and ST reads and writes it like any other row. A row
  with no companion is valid, always, and so is every companion. Which rows get
  one is generated: one per input, one per row a program that opted in
  (`quality` in `programs.csv`) writes, never an alarm. Opting in obliges the law
  to write every one of them, checked off matiec's write list. `Ref::get()`
  substitutes the declared value for a law unless the quality is usable
  (`Q_GOOD`, or `Q_HELD`, a law's own fallback that readers should compute on);
  `Var::value()` never substitutes, so a page shows the implausible reading.
- **Naming files a variable.** `var_group()` is the prefix of a name up to the
  first `_`; `published()` is true iff a row is neither a parameter, an alarm,
  nor a quality. There is no group table to add a variable to and none to forget.

### Key compile-time defines

| Define | Set when |
|---|---|
| `TARGET_SIMULATOR` | SIMULATOR build |
| `DEBUG` | SIMULATOR build |
| `_GNU_SOURCE` | SIMULATOR build |
| `TARGET_OPTA` | OPTA build |
| `ARDUINO` / `MBED_*` | OPTA build |

### Optional features (`BOTANIK_FEATURE_*`)

Optional subsystems are gated at compile time by the 23 `BOTANIK_FEATURE_*` flags
in `src/libbotanik_features.h`, which is GENERATED from the `[[features]]`
blocks of `src/libbotanik.toml` - one per flag, carrying its default, what it
requires and its prose, so adding a flag is that block plus a row in the
integration docs, and nothing re-derives the roster by reading the header. Each
flag defaults from an `#ifndef`-guarded macro, a
site names it in the `[features]` table of its `site.toml`, and the build can set
`-DBOTANIK_FEATURE_<X>=ON/OFF` (forwarded PUBLIC on the `botanik` library, same
ABI discipline as `BOTANIK_SITE_CONFIG`). Precedence: command line > site >
header default.

`BOTANIK_FEATURE_LIST` (an X-macro generated into the header) enumerates them
for anything that needs to: the `features` shell command expands it, and CMake
forwards `-D`s by matching the `BOTANIK_FEATURE_` prefix over its cache rather
than from a list. Adding a flag is one `[[features]]` block in
`src/libbotanik.toml` and a row in the integration docs; the `X()` entry, the
`#ifndef` default and any `#error` rule are all generated from it.

A build that sets nothing gets `CONSOLE` + `HOME_PAGE` + `SHELL_PAGE` +
`LOGGING`. Everything else is off, `PLC` included: `Plc::Runtime::programs[]`
and `PROGRAM_COUNT` are link-time symbols a site's generated `plant.cpp`
defines, so a build with no site cannot link it. A site with laws turns it on. A site states its whole profile in
the `[features]` table of its `site.toml`, `true` and `false` both, and
`tables.py config` generates `config.gen.h` from it. `ref` is the worked
full profile, and because the test and pyte builds compile the selected site, it
is also what keeps those suites exercising the whole surface. `check-alloff` is
the other end: every optional flag off and no site, existing to be compiled
rather than run.

| Group | Flags | Notes |
|---|---|---|
| core | `CONSOLE` (on), `LOGGING` (on) | `LOGGING` off leaves `Logs::Logger` an inert shell: `_emit()` returns before formatting, so no call site and none of the ~19 headers holding a `Logger*` change |
| network | `NETWORK`, `NTP`, `MQTT` | `NTP`/`MQTT` require `NETWORK` |
| pages | `HOME_PAGE`, `SHELL_PAGE` (both = `CONSOLE`), `LOGS_PAGE`, `ALARMS_PAGE`, `PARAMS_PAGE`, `MONITOR_PAGE`, `TASKS_PAGE`, `SYSTEM_PAGE`, `NOTES_PAGE` | all require `CONSOLE`; `LOGS_PAGE` also `LOGGING`. `SHELL_PAGE` gates the whole command interpreter, since `ShellPage` owns `Shell::CommandShell`. The boot page is whichever registers first, so it is positional |
| transports | `LOGS_PUBLISH`, `ALARMS_PUBLISH` (both need `LOGGING`+`MQTT`), `LOGS_MONITOR` (needs `LOGGING`) | `LOGS_MONITOR` is the secondary serial log sink, not `MONITOR_PAGE` |
| PLC | `PLC` | `PLC` off keeps `SiteDescriptor`'s program fields (removing trailing aggregate fields would break every site's initialiser) and does not remove control authority. The POU vocabulary is ST, compiled into the site, so there is nothing per-block to gate |
| field bus | `MODBUS_TCP_SERVER`, `MODBUS_TCP_CLIENT`, `MODBUS_RTU_CLIENT`, `MODBUS_RTU_SERVER` | `RtuClient` is the field-bus master scanning the site's field rows, `TcpSlave` serves the register map, `TcpMaster` is the sim's self-check against that map, `RtuSlave` the device model the simulated field is built from. `TCP_*` need `NETWORK` |
| standalone | `SUN_TRACKER` | owned by `BotanikFirmware` and reached through `SetupContext::sun`, so no site declares one. Off in the header, on in `ref/site.toml` |

A site's opt-in must yield to the command line, and `#ifndef` alone does not do
that: with `-DBOTANIK_FEATURE_CONSOLE=OFF` the page flags are still undefined
when the site config is read, so a bare `#define ..._PAGE 1` would turn the
builder's choice into a dependency error they never wrote. The generator emits
`#if !defined(X) || X` around each opt-in, nested under the prerequisites it
reads out of the `#error` rules, which is why a site states its profile in
`site.toml` rather than writing the guards by hand.

Anything reading a flag must include a `config/` header BEFORE
`libbotanik_features.h`: `config/base.h` pulls the site's config first, and
naming the feature header alone takes the defaults and loses whatever the site
turned off.

Gate with the preprocessor `#if BOTANIK_FEATURE_<X>`; it must drop member
declarations and ctor-init entries, and C++11 has no `if constexpr`. Four rules,
in order of preference:

1. **A `.cpp` guards its whole body**, with the flag its header uses, and the
   `#endif` is the last line of the file. Anything after it compiles against a
   class the header just gated away.
2. **Prefer an inert shell to gating a declaration.** `Logs::Logger` with
   `LOGGING` off keeps its type and guts `_emit()`: 3 guards inside `logger.cpp`,
   and no header changes. Declaration gating propagates to the member, its
   ctor-init entry, its accessor and every call site.
3. **Never gate a borrowed pointer or an aggregate field.** A forward-declared
   pointer costs nothing to declare, so the `#if` only moves to each consumer,
   which needs one anyway to dereference it. Gate the code, not the field.
4. **A site pins a flag with `#if !defined(X) || X`.**

`firmware/` holds about half the guards in the library, and that is the
composition root doing its job: it is the one place that knows which optional
subsystems exist. A guard appearing in a subsystem that could have used rule 2
or 3 is the smell.

## Site-specific code (greenhouse targets)

`libbotanik` is generic and site-independent. Code specific to one physical
greenhouse - its variable table, its control programs, and the mock data
animating them in the simulator - is a **site**. The reference site is
`ref`; treat it and `BotanikFirmware` as canonical.

| Layer | Holds |
|---|---|
| `libbotanik` | generic machinery plus `BotanikFirmware`, which owns the generic components and drives the per-tick `loop()` |
| site (`ref`) | the single source of truth for one greenhouse: its variable table, programs, shell commands, simulated field |
| target | thin: selects one site and runs it (`botanik_sim`, `botanik_fw`) |

A site has no subclass: `Site::firmware()`, generated into `vars.gen.h`, is a
`BotanikFirmware` bound to the site's `SiteDescriptor`. The base owns every page
and wires each table generically. `setup()` and `loop()` are non-virtual; a site
needing more derives `BotanikFirmware` and overrides `register_vars()`,
`register_pages()` (tab order, page 0 = boot page), `register_commands()`,
`register_telemetry()` or `on_setup()`. There is no per-phase hook on the
firmware: work at a point in the beat is a `Component` or an ST program.

A consumer project names its site with `botanik_add_site(SITE_DIR <dir>)`;
botanik's own tree picks an example with `-DBOTANIK_SITE=<name>`. The one other
thing a site writes by hand is `Site::simulate()` in its `sim.h`, which writes the
physical quantities through `botanik_sim/field.h` (`put()`, `at()`, `silence()`,
and the bus hooks `botanik_sim` fills in). Its tests compile against
`botanik_testkit`, which is `tests/kit/` and doctest: a change to either header
set is a change to what site repositories build against.

A site is data plus ST. Its rows, enums, home screen, field devices and
`SiteDescriptor` are generated from `tables/*.csv` and `site.toml` by
`scripts/sitegen/tables.py`; its laws are the ST under `programs/`, compiled by matiec.
Its enumerated codes are generated on both sides from `tables/enums.csv` - a C++
enum and an ST one that are the same type - so a law compares a phase against
`PHASE_DAY` rather than against 3. The library declares its own rows the same
way, in `src/libbotanik/tables/rows.csv`, one table rather than one per
kind, with a `guard` column for the feature flags a row needs.

A control program is one file, `programs/<stage>/<name>/<name>.st`, plus a row in
`tables/programs.csv` giving its instance, source and rate. No header, no class,
no registration: it names the rows it uses in `VAR_EXTERNAL CONSTANT` (read) and
`VAR_EXTERNAL` (written), and the body is the law. The site's own function blocks
are one file each, `blocks/<name>/<name>.st`. Its RS-485 field devices are
`tables/modbus_devices.csv` and `tables/modbus_models.csv`.

**The generated files are committed, and the build keeps them current.** A
generated tree (a site's `generated/`, and the library's own generated set)
carries a committed `codegen.stamp`: the SHA-256 over the name and content of
every input the generator reads (`cmake/codegen-lib.cmake` lists them). Each tree
is one `CODEGEN` custom command (`cmake/codegen.cmake`) depending on those inputs,
whose OUTPUT is a stamp in the build tree: the generated files are committed
sources, and a Makefile `make clean` deletes outputs. The command compares the
hashes in CMake; a match needs nothing, so a clean clone builds without python
or matiec. A mismatch runs `tables.py codegen`, which regenerates against the
pinned compiler and writes the stamp CMake computed; commit both. A mismatch without python3, jinja2
or (for a site with laws) matiec fails the build. `make codegen` is CMake's
builtin target, which runs these commands and nothing else. Configure never
looks for a generated file: it names `config.gen.h`, `rows.gen.h`,
`vars.gen.cpp` and, when `programs.csv` has rows, `plant.cpp`.

Only the selected site is regenerated by a build. A library input (libbotanik/tables/rows.csv,
a template) edited in botanik moves every example's stamp, and each example
catches up when `make examples` builds it; `gh_mlb41`'s regenerated output lands
in the submodule, to commit there. `plant` is a no-op on a site that declares no
programs: an ST resource with no program in it is not valid ST, so matiec is
never called for one. Generated documentation lives in `docs/generated/`: the
site's reference page, `docs/generated/variables.md` beside its `src/`, is
generated and stamped with the rest.

Generated sources carry `BOTANIK_SITE_CODEGEN_ABI` (in `rows.gen.h`), and
`vars/rows.h` refuses one that differs from `libbotanik/codegen_abi.h`. Bump
that number when the library stops accepting what the templates used to emit;
it catches output compiled without the build running codegen (the Arduino IDE,
a copied `generated/`).

The generator is `scripts/sitegen/`, driven by `scripts/sitegen/tables.py`: `model.py`
reads and checks the tables, `st.py` is the ST half, `reference.py` the
variables page, `paths.py` anchors it, and `render.py` is the Jinja environment
they emit through.

**It runs from anywhere.** `paths.ROOT` is derived from the generator's own
location, so what it reads out of the LIBRARY is anchored to the botanik
checkout wherever the command was invoked. What it writes for a SITE goes
beside that site, its reference page included, so a site repository
regenerates its own tables and its own docs and leaves the submodule alone.
Nothing may be relative to the working directory: `src/libbotanik/...` resolves
to the site repo when that is where the command was run.

`tables.py library` is botanik's own: `codegen_abi.h`, `config/capacities.gen.h`,
`vars/quality.h`, `core/health.h` and the two `plc/runtime/*.gen.h` accessor
blocks come from `libbotanik/tables/rows.csv`, its `enums.csv` and
`capacities.csv`, never from a site's tables, and they are committed here. A
site repository never runs it.

**The generator reads data, never C++.** Its inputs are CSV, TOML, ST and
matiec's own `POUS.c` output; a fact both C++ and the generated side need is
stated in a data file and the C++ is generated from it, not parsed out of it.
`libbotanik/tables/enums.csv` carries the members of `Vars::Quality` and `Plc::Health` and
feeds three outputs: the C++ enum, the constants matiec is handed, and a check
against the hand-written ST `TYPE` block (order is the wire contract, since
matiec numbers in declaration order). `config/capacities.csv` carries the sizes a
library row names as its capacity, so the string pool the generator lays out and
the buffer the library declares are one statement. `CODEGEN_ABI` lives in
`scripts/sitegen/model.py`. Nothing the generator reads is C or C++.

**`src/libbotanik.toml` is the library's own profile, the way `site.toml` is a
site's.** Its `[[features]]` blocks carry the flag roster, the defaults (from
which the always-on core is DERIVED rather than restated) and the prerequisites
the `#error` rules are emitted from. Its `[config]` table is the base value of
anything a site may move, and anything a library row names as its capacity has
to be there: a site's own `[config]` overrides it, `config.gen.h` emits the
override ahead of every default, and the string pool is sized from the
EFFECTIVE value - so a site that widens a host buffer widens the pool with it. Anything a generated file SAYS about a path is
written relative to the root, or the generating machine ends up in the diff. Every
generated file has a template of its own in `templates/`, so the shape of the
output is separate from the model that fills it. Regeneration needs `jinja2`;
without it the targets are not declared, as they are not without `iec2c`.

**The generator owns the checks.** It is the one thing that sees the whole table,
so it refuses at generate time what a runtime test cannot: names unique
under case folding, registers unique and inside their block, a mapped kind with
no register, local terminals unique per direction, a band end naming no row or
the wrong type, a default outside its own band, an alarm code too long for the
page. Nothing it checks is checked again at runtime.

`plant` is the command that runs matiec, and it checks what only the compiler's
answer can, off the `__SET_EXTERNAL` calls: a row declared writable that the body
never writes, an output/alarm/status row no program writes at all, a quality an
opted-in program owes and does not write, and a parking program that never
answers. A program naming a row no table declares fails to compile, because a
binding is a generated index.

What is left for the suites is behaviour, beside each site: `examples/ref/tests/`
and gh_mlb41's own `tests/`.

### Rules

- **One variable table per site, shared by simulator and hardware.** Never fork
  it per target; evolve the table and its `simulate()` animator together.
- **The simulator is a generic harness**, calling `Site::simulate()` each beat
  into the same rows a Modbus driver fills on hardware. For rows on the bus,
  `FieldBus` carries them through a simulated `RtuSlave` over a socketpair, so
  the sim exercises the driver rather than standing in for it.
- **The site stays portable.** It must cross-compile for the OPTA, which does not
  define `TARGET_SIMULATOR`, so simulator-only code stays behind that guard.
- **Generic components address variables by index**, through the generated
  `Vars::Row::<name>` constants (`generated/rows.gen.h`, reached by the library
  through `BOTANIK_SITE_ROWS`). A name no table declares is a compile error.
  `Bank::find()` stays for the console and for a log sink naming its own level
  row. The one trap: an index is a bare `uint16_t` and zero is a real row, so
  never read one back out of a `const` table it was declared in. `VAR_NONE` is
  0xFFFF for that reason.
- **Config is two tables and one GENERATED file per concern.**
  `libbotanik/tables/config/params.csv` is the parameter table - one row per
  value, `group,name,type,default,guard,desc` - and `checks.csv` the
  constraints over it, `group,expr,msg`. The generator emits one
  `libbotanik/config/<group>.gen.h` per group and a module includes the one it
  reads; `libbotanik_config.h` is only the aggregate. A `desc` is one short
  line with no `@brief` (the generator writes that) and no rationale, which
  goes to `docs/`. A check whose expression reads another group's value gets
  that header included, worked out from the names rather than stated.
  `config/base.h` is the preamble every concern header pulls first, and it
  reads the site's config before any default, then the feature flags. Nothing
  here is hand-written: an edit to a `.gen.h` is caught by the stamp and
  regenerated away.
- **Config is overridable at three levels.** Every value defaults from an
  `#ifndef`-guarded `BOTANIK_<NAME>`; a site moves one with a `name,value` row
  in its own `tables/config.csv`, which lands in its generated `config.gen.h`
  ahead of every default; and `-DBOTANIK_<NAME>=<value>` moves it on the
  command line, swept out of the CMake cache against the generated roster
  `config/names.gen.cmake` and forwarded PUBLIC. Precedence: command line >
  site > table default. A site's `site.toml` keeps identity, rates and
  features, and the generator refuses a `[config]` table left in it. `VAR_MAX`,
  `STR_POOL_BYTES` and the analog counts are COMPUTED from the variable table,
  so a site never states its own size.
- New site-level work goes in the site, not in `libbotanik` and not in a
  target's `main`.
