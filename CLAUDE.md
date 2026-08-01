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

The repo ships a `.envrc` that exports `MAKEFLAGS=-j4` and `CTEST_PARALLEL_LEVEL`
(so a bare `ctest` runs cases concurrently - serial it is ~14 s, parallel ~6 s),
so `make` builds in
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
  one CTest entry for the whole binary (`-tc=<name>` selects a case when debugging).
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
- the **doctest + pyte** suites via `ctest`: one entry per `TEST_CASE` (so a
  single case is runnable/debuggable from `ctest -R` or an IDE), plus one per
  pyte test file. Note each case gets its own process, so a test that corrupts
  state for later ones hides here - run `./build/tests/botanik_tests` directly
  to put them all in one process.

**clang-tidy is NOT in the gate** - it is a periodic sweep, not a per-change
check. Run it when a batch review is due:

```bash
make -C build clang-tidy       # incremental: one stamp per TU under build/clang-tidy/,
                               # so only sources whose text or included headers changed
                               # are re-analyzed (Makefile generator only)
make -C build clang-tidy-all   # the whole tree, ignoring the stamps
rm -rf build/clang-tidy        # force a full re-lint next time
```

It runs the `bugprone-*` checks from `.clang-tidy` with findings promoted to
errors, and exists only when `clang-tidy` was found at configure time.

The pyte suite needs its own release sim (RELEASE, no sanitizers, built in a
sub-tree under `build/pyte-sim/`). It is part of `all`, so a plain `make` builds
it and `make test` / `ctest` then have it - CTest itself never builds anything,
and CMake's built-in `test` target cannot be given dependencies. A screen test
that finds the sim missing fails loudly; it used to skip, which CTest reported as
`Passed`.

## Architecture

Everything is non-blocking: every stateful object exposes a `step()`/`loop()` doing a
small bounded amount of work and returning immediately; the main loop calls them in a
fixed order (blocking would stall the RTOS and trip the watchdog on the OPTA). Code
reaches the platform only through HAL, never raw `<time.h>` or platform APIs.

The library subsystems under `src/libbotanik/`:

**HAL (`hal/`)** - `platform.h` defines the unified interface, grouped into nested namespaces by concern: `Clock` (`current_ms()`, `uptime_ms()`, `wall_ms()`, `sleep_ms()`), `Memory` (`dsb()`, `dmb()`, `ordered_copy/move/read()`, `cache_clean/invalidate()`), `Stack` (`base/depth/high()`), `Debug` (`breakpoint()`, `under_debugger()`), `System` (`reset()`, `shutdown()`), `Watchdog` (`start/kick/stop()`), `Serial` (`Raw`, `Port`, `get()`), `Env` (`u32()`/`u8()`), `Storage` (`set/get/remove()` key-value persistence, `hal/storage.{h,cpp}`; Mbed KVStore on OPTA, one-file-per-key on the sim), and `Network` (the transport, in `hal/net.{h,cpp}`). `hardware_description()` stays at the `HAL::` top level. `simulator.h` implements it with `std::chrono` and Linux serial I/O; `opta.h` implements it for Mbed OS. All other code uses HAL exclusively - never raw `<time.h>` or platform-specific calls. The network transport (`hal/net.{h,cpp}`: link, DNS, UDP, TCP) is `HAL::Network`, mirroring the library's `Botanik::Network` components; the simulator backend uses real BSD sockets. `TARGET_OPTA` appears only under `hal/`; shared code never branches on it (`TARGET_SIMULATOR` may, for dev-only scaffolding).

**Scheduler (`scheduler/`)** - `LoopScheduler` is the non-blocking fixed-period loop driver: it releases one tick per period, handles overruns, and self-instruments per-beat (CPU duty cycle `(period - slept)/period`, scheduling jitter, work duration, and stack high-water via stack painting), publishing the metrics into a `SchedulerContext` surfaced in the status bar and the `stats` shell command.

**Firmware base (`core/firmware.h`)** - `BotanikFirmware` is the Template-Method base that constructs and owns every generic component, fixes the non-overridable per-tick `loop()` order (`tick -> serial.receive -> params -> io.expire_stale -> health -> net -> scan -> on_step -> logger -> terminal -> serial.transmit -> watchdog kick`), and exposes a reference to each component. Sites customize only through protected hooks: `register_pages()`, `register_commands()`, `register_telemetry()`, `on_setup()`, `on_step()` (see the Site section). The optional subsystem groups are bundled into feature-gated stacks (`core/stacks.h`: `MonitorStack` / `ConsoleStack` / `NetworkStack`).

**Control authority** - whether the firmware may drive the field is one latch, `Vars::Bank::control_enabled()`, and `disable_control()` / `enable_control()` are the only ways it moves. Disabling drops every `ROLE_OUTPUT` to its declared safe state, releases any override holding one, and inhibits the scan. It starts **disabled**, so nothing between power-on and the end of `setup()` can energise an actuator on the way past. It is withdrawn on a health fault, and again before the loop stops kicking the watchdog (a deliberate reset gives no guarantee about the outputs, and that gap is exactly when one must not be left energised); the shell's `reset` / `exit` do the same. `control enable|disable` from the console toggles it by hand. A latch, not a per-beat sweep - sweeping after the scan would flicker, since the next beat's tasks re-drive the same demands. Two **independent inhibits** hold it down - `INHIBIT_OPERATOR` (the console) and `INHIBIT_HEALTH` (bring-up, a health fault, a pending reset) - and control returns only when both let go. That is why it is a set and not a flag: an operator who typed `control disable` stays disabled across a health fault that comes and goes, and equally the console cannot talk over the firmware's own judgement that it must not drive. `control` reports which holds are in force, so a `control enable` that changes nothing reads as a health fault rather than a broken command.

**Component / context (`core/component.h`, `core/context.h`)** - `Component` is a mixin base giving each subsystem a one-shot first-step BOOT/DEBUG announce. `SystemContext` is a borrowed-pointer aggregate the terminal UI and the shell read: the per-subsystem status contexts (scheduler, network, NTP, MQTT) plus `vars`, the `Vars::Bank`. Wiring a consumer means reading the context, not adding a setter for it.

**Variables (`vars/`)** - the one model everything the firmware exposes goes through: a Modbus meter reading, a relay coil, a computed global, a configuration parameter. They differ along three independent *fields*, never types: `VarType` (what the value is - `T_BOOL`/`T_U16`/`T_F32`/`T_ENUM`/`T_STR`...), `Transport` (how it reaches the field: `XP_NONE`/`XP_LOCAL_DIGITAL`/`XP_LOCAL_ANALOG`/`XP_MODBUS`), and `VarRole` (what it is FOR, which drives behaviour: `ROLE_INPUT` expires past its timeout, `ROLE_OUTPUT` drops to safe on a fault, `ROLE_INTERNAL` never expires, `ROLE_PARAM` persists and is editable). Files:

- `scalar.h/.cpp` - `Scalar`, the 8-byte union every value, bound and default lives in, plus the type-driven read/write/render/parse (`as_f32`/`from_i64`/`format_scalar`/`parse_scalar`/`validate_str`). `constexpr` constructors keep the site's tables constant-initialised straight into flash.
- `model.h/.cpp` - `Variable` (the const descriptor, flash-resident) and `Value` (the parallel RAM slot, pinned at 32 bytes); `Var`, the one non-owning (descriptor, slot) handle, made on the fly and passed by value; `VarRef`, a bare index; `VarTables` + `Bank`, the table plus its value store and string pool. A bad index resolves to an inert **sink** rather than clamping onto a real row. `Bank::init()` seeds defaults, `expire_stale()` invalidates timed-out inputs, `disable_control()` / `enable_control()` gate driving the field. The `def` field does three jobs: an input's substitute when invalid, an output's safe state, and a parameter's factory value.

  **Bank is the one object that gets, sets and overrides variables**, and it is what `SystemContext::vars` exposes - consumers reach it there rather than each being handed its own pointer. Two distinct writes: `set()` edits a **parameter** (validated against its declared band, persisted through the `ParamStore`, `SET_INVALID` for anything else - writing a sensor or an output would just be overwritten by the next scan); `override_set()` **forces any variable** (volatile, range-checked against the type only, so you can push a reading past its plausibility band to exercise the alarm watching for it).

  **Overrides** (`Value::ovr` / `ovr_on` / `ovr_valid`) are a maintenance tool: force a sensor, freeze a computed value, drive an actuator by hand. Applied in `Var::raw()`/`valid()` - the one place every reader goes through - so an override reaches control logic, console, telemetry and alarms alike. It *shadows* rather than replaces: the driver keeps publishing underneath, so clearing snaps back to live with nothing to restore. `ovr_valid` is separate so "force this to 20 C" and "make this read as a dead sensor" are both expressible. Never persisted, cleared by `init()`. Control authority outranks an override, never the other way round: `disable_control()` **releases** an override on an output (released rather than ignored - a staged one would fire the moment control returned), and while control is off `override_set()` refuses an output outright.
- `params.h/.cpp` - a parameter is just a `ROLE_PARAM` variable. `Params` is the editable view over that (contiguous) block: dense 0-based indexing for the PARAMS page and the shell, the boot load, and `set()` returning `SET_OK`/`SET_INVALID`/`SET_NOT_PERSISTED` (a refused *persist* is not a rejected *edit*). `KvParamStore` is one `HAL::Storage` key per parameter, named after it - no layout to version. Config-driven components read by name (`Vars::get_int/get_f32/get_str`) and re-apply their group when its commit serial (`group_serial(io, "net")`) bumps.
- `access.h` - `VarGroup` (a sub-topic + the `VarRef`s under it; the unit shared by the MONITOR page, the `status` command and MQTT telemetry), `HomeVar` (a home-screen label + ref), and `var_format()`.

**Scan cycle (`scan/`)** - `Scan::Driver` runs the site's control programs at their declared rates, stepped once per beat between the input refresh and the output write. A `Scan::Task` is `{name, period_ms, run(ScanContext&)}`, const and flash-resident. Inputs refresh once per beat so every task in a beat sees the same instant; a task due late runs **once** (missed activations are dropped, not caught up), so `ScanContext::dt_ms` is the ACTUAL elapsed time since that task last ran, never the nominal period. `blocks.h` is the function-block library programs are built from - `Ton`/`Tof`/`Tp`, `RTrig`/`FTrig`, `Sr`/`Rs`, `Hysteresis`, `Ramp`, `Deadband` - each taking `dt_ms` rather than reading a clock, so they are testable without one.

**Console UI (`console/`)** - two layers. Which terminal is on the wire is `Config::TERMINAL_TYPE` (`Terminal::Ansi::TerminalType`: VT100 / VT102 / MINITEL_1B / MINITEL_2), a site config default passed to the `Console` constructor - a property of the installation, not something a harness selects. It picks the one-time setup handshake `setup_terminal()` emits (the Minitels get their PRO2 sequences) and is what any terminal-specific behaviour branches on (`Ansi::is_minitel()`). `AnsiTerminal` is the VT **device** and owns a screen back-buffer: renderers draw immediate-mode through the usual ops (`move_to` / `write_chars` / `set_inverted` / `scroll_region` ...), which paint into an in-memory FRONT cell grid rather than the wire. Once per tick `present()` diffs FRONT against BACK (what is on the wire), emits the minimal ANSI to reconcile them - paced under a per-tick byte budget derived from the link baud - and copies the emitted cells into BACK. Renderers therefore keep no "what did I draw" shadow: redrawing an unchanged cell costs nothing. It also spots a row that merely slid sideways and sends one insert/DCH instead of rewriting the tail, and visits rows by priority class (`RowPriority`) so an exhausted budget drops the least important rows rather than whatever sat lowest. See `docs/dev/screen.md`; the line editor is the one deliberate exception (`docs/dev/editor.md`). `Console` composes an `AnsiTerminal` (exposed via `terminal()`) and drives the multiplexed page display: it routes input to the active page, owns the row-0 status bar and the bottom alarm bar, reserves the content area (`rows()`), and runs the per-tick `step()`. Renderers (pages, editor, bars) hold a `Console&` and paint through its `terminal()`. `InputParser` decodes raw byte streams into `InputEvent`s (handles X3.64 CSI, Alt, and function-key escape sequences). `TextEditor`/`InputEditor` is a byte-efficient line-editing state machine that consumes `InputEvent`s. The layer minimizes escape bytes emitted (low-bandwidth serial link).

**Pages (`pages/`)** - Full-screen views behind a common `Page` interface; `Console` routes input to the active page and calls its render method on demand. All are base-owned and site-independent. Core pages: `HomePage` (landing screen: logo, firmware identity, uptime, a live log-tail band, and up to 22 declarative site "home fields" - label + variable - laid out in three columns of 8, the first two cells of column 1 going to the uptime readout and a spacer), `Shell`, `Logs`, `AlarmsPage` (scrollable alarm-condition table, filter modes recent/all/active/unmasked, selection cursor + `a` to ack; a panel below the table shows the selected alarm's remediation note and ack time), `ParamsPage` (parameter table with name/type/value/default/unit columns over `Vars::Params`; a detail panel below the table shows the selected parameter's minimum/maximum/description/group, and ENTER opens an inline bottom-row value editor), `MonitorPage` (scrollable, read-only table of the site's status variables with group/name/value/unit columns + a description/source/quality detail panel, generic over a `VarGroup[]` + `Bank` - the same table the `get` command and MQTT telemetry read). `StatusPage` (a two-column system health grid, templated on a site data struct) is a kept building block but **not** in the default tab order - a site re-adds it by subclassing it and overriding `register_pages()`. Building blocks: `MetricPage` (metric display base; repaints fields whole and keeps a value cache only to drive the watch-style change highlight, which the terminal's diff cannot infer), `Scrollback<L>` (incremental-redraw circular buffer, bounded per beat) over `LineBuffer<L,N>` (fixed-capacity circular storage with soft-wrap).

**Shell (`shell/`)** - `CommandShell` handles command submission, built-in dispatch, command history (up/down recall), and a kill-ring (Ctrl-K/Ctrl-Y). Built-ins: `help`, `clear`, `logs`, `stats`, `uptime`, `version`, `net`, `ntpsync`, `alarms`, `storage`, `loglevel`, `breakpoint`, `reset`, `exit`, plus the three that work the variable table - `get [<name>]` (read anything), `set <param> <value>` (edit + persist a parameter), and `override [<name> [off|<value>]]` (force any variable for maintenance; bare name freezes it at its current value, bare `override` lists what is forced, `override off` clears the lot), and `control [enable|disable]` (whether the firmware may drive the field at all). They read `Vars::Bank` from `SystemContext`, so the shell has no per-consumer setter. Sites extend it with a registered `ShellCommand[]` table.

**Logging (`logs/`)** - `Logger` uses a dual-bucket token-bucket rate limiter (one global bucket, one per topic) before forwarding to registered `LogWriter`s. `LOG_CRITICAL` entries are undroppable (they evict the oldest queued entry instead of being dropped), and silent-loss categories (sink drops, TX-full truncation, rate-limiter suppressions) are reported edge-triggered as criticals. Writers include `BufferedLogWriter`, `SerialLogWriter`, and (behind `BOTANIK_FEATURE_MQTT`) `MqttLogWriter`, which publishes each entry to the broker under `<MQTT_TOPIC_PREFIX>/<MQTT_LOG_TOPIC>/<level>/<topic>`. Log levels and topics are the `LogLevel` / `LogTopic` enums in `logs/topics.h`. Topics: `SHARED_IO`, `BOOT`, `NETWORK`, `TIME`, `STATUS`, `ASSERT`, the alarm transitions `ALARM_START`/`ALARM_END`/`ALARM_FLAP`, `HEALTH`, and `STORAGE`. The numbering is deliberately not compacted - it is what sits in already-written log records.

**Alarms (`alarms/`)** - `AlarmModel` holds one `AlarmCondition` per site-declared alarm; each names the boolean variable that carries it (a `VarRef`), OR'd with a local override. There is **no aggregate bitset**: `step()` reads each condition's raw input, advances dwell, and raises ALARM-topic log lines and notifier callbacks on thresholds. `MqttAlarmNotifier` formats alarm transitions as MQTT topics/payloads (`alarms/publisher.h` is the transport interface). `platform.h` defines the generic platform-health alarms (SYS_RST, CPU load, jitter, stack, overrun, flash fault), appended after the site's so site conditions fill from index 0.

**Health (`health/`)** - `HealthMonitor` owns the `AlarmModel` and an `MqttAlarmNotifier`, asserts the platform-health alarms (`alarms/platform.h`) from the scheduler metrics and the storage fault count, runs a power-on self-test (POST: the platform alarm codes must resolve, the logger must have writers), and produces the watchdog verdict. That verdict is now simply "the loop is alive": with no coprocessor link to lose, nothing else justifies letting the hardware watchdog fire, and control-side fault escalation belongs to the scan cycle.

**Network (`network/`)** - Wired into `BotanikFirmware` behind `BOTANIK_FEATURE_NETWORK` / `_NTP` / `_MQTT` (default off; on for `gh_mlb41`). Functional on OPTA; on the simulator the `hal/net.cpp` backend is hermetic - it emulates DNS/NTP/MQTT in-process (a name resolves to a fixed mock peer after a short async delay, NTP queries get a canned reply, MQTT connects) so the handlers run their real state machines against a deterministic, offline peer with no host network I/O. `NetworkHandler` brings up the network device and hands it to `NameResolver` (non-blocking DNS over Mbed's async resolver, poll-style API + cache); `NtpClient` runs NTP through the resolver; `MqttClient` is a minimal non-blocking MQTT 3.1.1 publisher; `MqttLogWriter` (`mqtt_log.{h,cpp}`) is a `LogWriter` that publishes log entries through it (best-effort, own min level).

**Sun tracking (`sun/`, gated by `BOTANIK_FEATURE_SUN`)** - in the `Botanik::Sun` namespace, all in `sun/sun_tracker.{h,cpp}`: `Sun::SunPositionTracker` is a config-driven `Component` tracking the sun's current elevation angle above the horizon for the site location, on top of `solar_elevation()`, the NOAA solar-position math (uptime-ms domain, angle in radians). Standalone (not instantiated by `BotanikFirmware`); default on, `-DBOTANIK_FEATURE_SUN=OFF` drops the `sun/` sources and their tests.

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

Optional subsystems are gated at compile time by `BOTANIK_FEATURE_*` flags in `src/libbotanik_features.h`, which mirrors the `BOTANIK_*` config-override pattern: each defaults from an `#ifndef`-guarded macro, a site can `#define` it in `sites/<name>/config.h`, and the build can set `-DBOTANIK_FEATURE_<X>=ON/OFF` (forwarded PUBLIC on the `botanik` library, same ABI discipline as `BOTANIK_SITE_CONFIG`).

| Flag | Default | Gates |
|---|---|---|
| `BOTANIK_FEATURE_NETWORK` / `_NTP` / `_MQTT` | off | the `network/` stack (`NetworkHandler`/`NameResolver`/`NtpClient`/`MqttClient`), bundled as `NetworkStack` and stepped by `BotanikFirmware`; on for `gh_mlb41`. `NTP`/`MQTT` require `NETWORK` (enforced by `#error`) |
| `BOTANIK_FEATURE_SUN` | on | the `sun/` solar-position tracker (`Sun::SunPositionTracker` + the `solar_elevation()` NOAA math) and its tests. Standalone - not instantiated by `BotanikFirmware`; a site owns it |
| `BOTANIK_FEATURE_CONSOLE` | on | the interactive ANSI UI: `Console` (over the `AnsiTerminal` VT device), the built-in shell/logs/alarms pages, the console UART, and the `register_pages()` / `register_commands()` hooks. Off -> headless |
| `BOTANIK_FEATURE_MONITOR` | on | the secondary MONITOR UART and its `SerialLogWriter` sink. Independent of `CONSOLE`: a headless build can keep MONITOR for diagnostics, an interactive build can drop it. Both off -> logs reach only site-registered sinks |

Gate with the preprocessor `#if BOTANIK_FEATURE_<X>` (it must drop member declarations and ctor-init entries; C++11 has no `if constexpr`). See `docs/integration/config.md`.

## Site-specific code (greenhouse targets)

`libbotanik` is generic and site-independent. Code specific to one physical greenhouse (its variable table, its alarm table, its control programs, and the mock data that animates them in the simulator) is a **site**, code-named per greenhouse. The reference site is `gh_mlb41`.

Three layers, generic to specific:

| Layer | Holds | Examples |
|---|---|---|
| `libbotanik` | generic, site- and target-independent machinery, plus the `BotanikFirmware` base that owns the generic components, exposes a reference to each on the instance, and drives the per-tick `loop()` | `Vars::Bank`, `Scan::Driver`, `Console`, `AlarmModel`, `BotanikFirmware` |
| site (`gh_mlb41`) | single source of truth for one greenhouse: the variable table (`vars.h`), the declarative tables (`VarGroup[]` status groups + `HomeVar[]` home fields, `ShellCommand[]`, `g_alarms[]`, `Scan::Task[]`), the `SiteDescriptor` bundling them, the `simulate()` field animation, and a thin `GreenhouseMLB41` class deriving `BotanikFirmware` (no pages, no hook overrides) | `gh_mlb41` |
| target / entry point | thin: selects one site and runs it | `botanik_sim` harness, generated OPTA `.ino` |

**Composition.** Components are split by layer and chained by inheritance (not an ideal composition model, but a structured start):

- `BotanikFirmware` constructs the generic components (logger, scheduler, variable bank, parameter store, scan driver, health/alarm model, terminal, core pages) and exposes a reference to each on the instance.
- `GreenhouseMLB41 : BotanikFirmware` is a thin binding: it passes a `SiteDescriptor` (name + the shell-command / status-group / home-field / alarm / variable / task tables) to the base constructor. The base owns every page and wires each table generically; the site declares no page members and overrides no hooks (a site needing custom per-beat logic or an extra page would override `on_step()` / `register_pages()`).
- A site component is often a static site-defined table registered on a base-owned generic component: the site `g_alarms[]` table feeds `AlarmModel` (which appends the generic platform-health alarms from `libbotanik/alarms/platform.h`, so site conditions fill from index 0), a `ShellCommand[]` table registers on the shell page via `set_commands()`, a `Scan::Task[]` table on the `Scan::Driver`.
- Lifecycle is fixed by the base: `setup()` and `loop()` are non-virtual, and a site hooks in through `on_setup()` / `on_step()`.

Rules:

- **One variable table per site, shared by simulator and hardware.** A site's `vars.h` table is used verbatim by both the OPTA firmware and the simulator. Never fork it per target; evolve the table and its `simulate()` animator together, in the site.
- **The simulator is a generic harness.** It calls the selected site's `Site::simulate()` from the main loop each beat, writing the very same variables a Modbus driver writes on hardware. The site code is identical either way.
- **The site stays portable.** Site code must cross-compile as an Arduino library: the deploy step zips the site dir and generates a `.ino` that statically allocates a `GreenhouseMLB41` and calls its `setup()` / `loop()`. The zip build does not define `TARGET_SIMULATOR`, so simulator-only code (the `simulate()` animator) drops out through its `#ifdef TARGET_SIMULATOR` guard. Keep such code guarded rather than in the portable path. Site includes are **relative**, because the Arduino PLC IDE adds no include directories inside a sketch.
- **Generic components address variables by reference, not offset.** Each alarm names the `VarRef` of the boolean carrying it (the `AlarmModel` reads it per condition), and each status group / home field names the `VarRef`s it shows. There is no byte layout to satisfy - a bad index resolves to the inert sink rather than aliasing a real row.
- **Config is site-overridable.** Every `libbotanik_config.h` value defaults from an `#ifndef`-guarded `BOTANIK_<NAME>` macro. A site may ship `sites/<name>/config.h` that `#define`s any of them; CMake includes it via `BOTANIK_SITE_CONFIG`, defined PUBLIC on the `botanik` library so the override is build-wide (libbotanik's own TUs and every consumer see the same value - anything narrower is an ABI mismatch).
- New site-level work goes in the site, not in `libbotanik` and not in a target's `main`.

**Current state.** The `gh_mlb41` site lives in `src/sites/gh_mlb41/`, the `BotanikFirmware` base in `src/libbotanik/core/firmware.h`, and `GreenhouseMLB41` derives it. The site files split generated from hand-written:

| File | Origin | Holds |
|---|---|---|
| `vars.h` / `vars.cpp` | generated | every variable in one table (inputs, outputs, internals, parameters) + its RAM value store and string pool |
| `status.h` | generated | the display groups (`VarGroup[]`) and home-screen readings (`HomeVar[]`), as `VarRef`s |
| `alarms.h` | generated | the `g_alarms[]` condition table |
| `descriptor.h` | generated | the one `SiteDescriptor` bundling them all |
| `commands.h` | hand-written | site shell commands (code, not schema) |
| `tasks.h` | hand-written | the control programs (code, not schema) |
| `sim.h` | hand-written | the simulated field; whole body `#ifdef TARGET_SIMULATOR` |
| `site.h` | hand-written | the `GreenhouseMLB41` binding + the uniform `BotanikSite` handle |
| `config.h` | hand-written | build-wide config overrides, kept separate so CMake includes it before the library defaults |

A site does **not** override `setup()`/`loop()`; it customizes through the protected Template-Method hooks: `register_pages()` (tab order, page 0 = boot page), `register_commands()`, `register_telemetry()`, `on_setup()` (post-wiring), and `on_step()` (per-beat). See the worked extension example in `core/firmware.h`. Both targets run the same site; the site is chosen at configure time with `-DBOTANIK_SITE=<name>` (default `gh_mlb41`): CMake globs `src/sites/<name>/` and points `BOTANIK_SITE_HEADER` at its `site.h`, which exposes `BotanikSite`, so the harnesses never name the site. Only the Arduino `.zip`/`.ino` deploy packaging remains. Treat `src/sites/gh_mlb41/` + `BotanikFirmware` as canonical.
