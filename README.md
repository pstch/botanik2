# botanik // small-scale horticultural greenhouse control system

![Botanik logo](images/logo.png "Botanik")

This project hosts the programming of a control system for a small-scale
horticultural greenhouse. It handles irrigation (on fixed schedules) and
ventilation (air blowers, intractors, window openers, thermal screens and an
evapo-transpiration cooling system). It runs alongside other autonomous
systems - an emergency heat blower and a heatpump, each on their own
thermostat - and monitors their operation, the power supply, and the emergency
generator.

The system runs on an Arduino OPTA PLC (STM32H747 dual-core ARM) with a few
Modbus RTU I/O modules, which lets us run custom C++ alongside the PLC
programming. That custom code exists to work around the many shortcomings of
Arduino's PLC platform - which is frustrating, largely undocumented, and
missing features standard elsewhere. This repository is therefore also a place
to capture the knowledge we've gathered about that platform.

## Library structure

`libbotanik` is a C++11 library, split into independent subsystems under
`src/libbotanik/`. It is platform-agnostic: everything goes through a Hardware
Abstraction Layer so the same code builds as a Linux simulator or for the OPTA.

| Subsystem | Path | Responsibility |
|---|---|---|
| **HAL** | `hal/` | Unified platform interface (clocks, memory ordering, stack/PLC introspection, serial). `simulator.h` and `opta.h` implement it. |
| **Scheduler** | `scheduler/` | Non-blocking fixed-period loop driver with overrun handling and built-in CPU-load, scheduling-jitter, work-duration, and stack-usage instrumentation. |
| **Console UI** | `console/` | `Console` drives a multiplexed page display over the `AnsiTerminal` VT device; `InputParser` decodes byte streams into events; `TextEditor` is a byte-efficient line editor. |
| **Pages** | `pages/` | Full-screen views (`Shell`, `Logs`, `Home`, `Status`) behind a common `Page` interface. |
| **Logging** | `logs/` | `Logger` rate-limits with a dual token-bucket, then fans entries out to `LogWriter`s. |
| **PLC I/O** | `plcio/` | `PlcSyncHandler` runs RX/TX state machines over shared memory with the PLC coprocessor; an emulator stands in for it on the simulator. |
| **Utils** | `utils/` | `BufferedSerial` (ring-buffered UART), `filters.h` (reusable EMA / windowed-max metric filters), and time/uptime helpers. |

The application wires these together and runs them from a single non-blocking
loop - see `src/botanik_sim/main.cpp` for the canonical setup (the OPTA `.ino`
sketch does the equivalent).

## Key concepts

- **Nothing blocks.** Every stateful object exposes a `step()` that does a
  small, bounded amount of work and returns immediately; the main loop calls
  them in a fixed order. Blocking would stall the RTOS and trip the watchdog on
  the OPTA. See [CONTRIBUTING.md](CONTRIBUTING.md) for the full contract.
- **HAL-only platform access.** Code never calls `<time.h>` or platform APIs
  directly - only `HAL::current_ms()`, `HAL::dsb()`, `HAL::ordered_copy()`, etc.
  This is what makes the simulator possible.
- **PLC shared memory.** Host and PLC coprocessor communicate through shared
  memory using transactional read/write patterns and explicit ARM DSB barriers
  to avoid torn reads, with heartbeat / timeout / watchdog tracking.
- **Rate-limited logging.** A global bucket plus a per-topic bucket throttle log
  volume; suppressed-message counts are reported on a slower cadence.
- **Byte-efficient terminal.** The UI targets a low-bandwidth serial link, so
  the console/editor layer minimizes the escape sequences it emits.
- **Self-instrumenting loop.** `LoopScheduler` measures per-beat CPU load (the
  duty cycle, `(period − slept)/period`), scheduling jitter, work duration, and
  stack high-water usage (the last via stack painting), surfaced live in the
  status bar and the `stats` shell command.

## Building

The default target is the Linux simulator (`SIMULATOR`), which compiles to a
native executable and is the only target that *runs* end-to-end today (the OPTA
target compiles but isn't linked into a flashable image yet - see below). Builds
default to a profiler-friendly `DEBUG` type:

```bash
cmake -B build-debug -DBOTANIK_TARGET=SIMULATOR
make -C build-debug
./build-debug/botanik_sim        # run the simulator
```

DEBUG builds enable the AddressSanitizer + UndefinedBehaviorSanitizer by default
(`BOTANIK_SANITIZE=address+undefined`); the interactive sim runs fine instrumented
and it catches UB/memory bugs early. Turn them off with `-DBOTANIK_SANITIZE=OFF`.

For an optimized build, add `-DCMAKE_BUILD_TYPE=RELEASE` (enables `-O3`/LTO,
compiles assertions out, and defaults sanitizers off).

### OPTA target

The `OPTA` target cross-compiles the library and firmware for the Arduino OPTA
PLC with `arm-none-eabi-gcc`:

```bash
arduino-cli core install arduino:mbed_opta   # one-time: the Mbed OPTA core
cmake -B build-opta -DBOTANIK_TARGET=OPTA
make -C build-opta
```

This builds `libbotanik` plus the `botanik_fw` reference firmware into static
archives - an ABI / size / LTO sanity check for the target, **not** a flashable
image. The final link (with the startup code and the PLC IDE's generated types)
happens in the Arduino / Axel PLC IDE; see
[src/botanik_fw](src/botanik_fw/README.md) for how the pieces map. The Mbed
headers and ARM toolchain come from the core that `arduino-cli` installs under
`~/.arduino15`; the build auto-discovers the highest installed version (override
with `-DMBED_CORE_ROOT=...` or the `ARDUINO_DATA_DIR` env var).

### Tests

Unit tests use [doctest](https://github.com/doctest/doctest) (vendored as a
single header under `tests/vendor/`) and run only in the `SIMULATOR` build,
linked against the host library. From a configured SIMULATOR tree:

```bash
make -C build-debug check          # the full pre-push gate (see below)
```

`check` is the one-shot pre-push gate. It runs three things and fails on the
first problem:

- a **warning-clean compile** - the `botanik_strict` target mirrors the library
  with `-Werror`, so any compiler warning fails the build (plain `make` stays
  warning-tolerant - visibility, not a hard gate);
- **clang-tidy** - the `bugprone-*` checks from `.clang-tidy`, promoted to errors;
- the **doctest unit suite** via `ctest`.

The pieces, if you need them individually:

```bash
make -C build-debug clang-tidy     # just the static analysis
make -C build-debug botanik_tests  # just build the test binary
./build-debug/tests/botanik_tests  # run it directly (doctest CLI)
./build-debug/tests/botanik_tests -s   # ...showing every passing check, incl. the struct-size table
ctest --test-dir build-debug --output-on-failure
```

The suite covers the `format_float`/`format_age` text helpers, the time
utilities (rollover projection + calendar conversion), and a `sizeof` test that
documents the in-memory footprint of the big UI/PLC structures.

## Documentation & useful links

- [CONTRIBUTING.md](CONTRIBUTING.md) - code style, naming, and the non-blocking design rules.
- [CLAUDE.md](CLAUDE.md) - architecture summary and build notes for AI coding agents.
- [docs/EVENT_DISPATCH.md](docs/EVENT_DISPATCH.md) - terminal input event flow.
- [docs/KEYBINDINGS.md](docs/KEYBINDINGS.md) - terminal key bindings.
- API reference - `make -C build-debug docs` regenerates the HTML under
  `docs/doxygen/html/` (or run `doxygen Doxyfile` directly). `make -C build-debug
  docs-open` builds the tests + docs, runs the suite, and opens the result in a
  browser. The generated [Testing](docs/doxygen/html/testing.html) page covers
  the test suite and the struct-size table.

### External

- [Arduino OPTA](https://www.arduino.cc/pro/hardware/product/opta/)
- [Modbus Application Protocol](https://modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf)
- [ANSI escape codes](https://en.wikipedia.org/wiki/ANSI_escape_code)
