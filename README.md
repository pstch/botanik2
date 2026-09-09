# botanik // small-scale horticultural greenhouse control system

![Botanik logo](images/logo.png "Botanik")

Control system for a small horticultural greenhouse. It runs irrigation on
fixed schedules and drives ventilation: air blowers, intractors, window
openers, thermal screens and an evapo-transpiration cooling system. An
emergency heat blower and a dehumidifier run on their own thermostats; botanik
monitors them, along with the power supply and the emergency generator.

The target is an Arduino OPTA PLC (STM32H747, dual-core ARM) with a few Modbus
RTU I/O modules. We run custom C++ on it rather than the vendor PLC tooling,
which is undocumented and misses a lot of important features.

The library may also be used for its serial console implementation, a console
that supports a large set of useful features for PLCs and that provides a
a complete HMI with parameter control, variable observability, logging, 
input/output overrides, etc. Feature flags provide the ability to disable
specific components and allow, for example, to have a console with just a 
shell page, or a console with just a logs page, etc.

## Library structure

`libbotanik` is a C++11 library under `src/libbotanik/`, split into
subsystems. All platform access goes through a HAL, so the same code builds as
a Linux simulator, as an OPTA firmware or as a firmware for a ported platform.

TODO: visually align the table below

| Subsystem | Path | Responsibility |
|---|---|---|
| HAL | `hal/` | Clocks, memory ordering, stack and system introspection, serial, storage, network. `simulator.h` and `opta.h` are the two backends. |
| Scheduler | `scheduler/` | Fixed-period loop driver. Handles overruns and measures CPU duty, jitter, work duration and stack high-water per beat. |
| Tasks | `tasks/` | The task table and `TaskTimer`. The scheduler decides when a beat happens, the registry decides what runs in it. |
| Console | `console/` | `AnsiTerminal` is the VT device; `Console` multiplexes pages over it; `TextEditor` is the line editor. |
| Pages | `pages/` | Full-screen views behind a common `Page` interface. |
| Shell | `shell/` | `CommandShell`. Commands are noun then verb. |
| Logging | `logs/` | `Logger`, rate-limited by a dual token bucket, fanning out to `LogWriter`s. |
| Alarms | `alarms/` | `AlarmController` over an alarm source table, with per-alarm dwell policy. |
| Health | `health/` | Platform-health alarms, the power-on self-test, and the watchdog verdict. |
| Variables | `vars/` | One table of rows - readings, coils, computed values, parameters, alarms - and the RAM over it. |
| PLC | `plc/` | The IEC 61131-3 vocabulary the control programs are written in, in ST, plus the runtime that joins matiec's output to the beat. |
| I/O | `io/` | The controller's own input terminals, published like any field row. |
| Modbus | `modbus/` | RS-485 field bus and plant network, master and device model on each. |
| Network | `network/` | Link bring-up, name resolution, NTP and MQTT, all non-blocking. |
| Sun | `sun/` | Solar position, from the NOAA math. |
| Utils | `utils/` | Ring-buffered serial, metric filters, time helpers. |

`BotanikFirmware` owns the components and drives the beat. A site supplies the
tables and control programs it runs on; `src/botanik_sim/main.cpp` is the
canonical wiring.

## Key concepts

- **Nothing blocks.** Every stateful object has a `step()` that does a small
  bounded amount of work and returns. Blocking stalls the RTOS and trips the
  watchdog on the OPTA. `CONTRIBUTING.md` has the contract.
- **Platform access goes through the HAL.** No `<time.h>`, no Arduino calls
  outside `hal/`. This is what makes the simulator possible.
- **One variable model.** A Modbus reading, a relay coil, a computed value and
  a configuration parameter are rows of one table, differing by field rather
  than by type.
- **The console is byte-efficient.** It targets a 4800-baud serial link, so the
  terminal and editor layers minimise what they emit.

## Building

Requires CMake 3.10+ and a C++11 compiler. The default target is the Linux
simulator; builds default to `DEBUG` (`-O0 -g3`).

```bash
cmake -B build -DBOTANIK_TARGET=SIMULATOR
make -C build
./build/botanik_sim
```

`-DCMAKE_BUILD_TYPE=RELEASE` gives `-O3`, LTO and `NDEBUG`.
`-DBOTANIK_SANITIZE=address+undefined` instruments a tree; sanitizers are off
by default and `make check-full` runs the gate instrumented in a tree of its
own.

`.envrc` exports `MAKEFLAGS=-j4` and `CTEST_PARALLEL_LEVEL` for
[direnv](https://direnv.net) users. `ccache` is used when installed.

### OPTA target

```bash
arduino-cli core install arduino:mbed_opta   # one-time
cmake -B build-opta -DBOTANIK_TARGET=OPTA
make -C build-opta
```

This links `botanik_opta`, the flashable M7 image; the entry wiring is in
[src/botanik_fw](src/botanik_fw/README.md). The Mbed headers and the ARM
toolchain come from the core `arduino-cli` installs under `~/.arduino15`. The
build takes the highest version installed; override with `-DMBED_CORE_ROOT=...`
or `ARDUINO_DATA_DIR`.

## Tests

Two host-only suites, both under CTest: [doctest](https://github.com/doctest/doctest)
unit tests in `tests/`, and pyte screen tests in `tests/pyte/` that drive
`botanik_sim` over a PTY.

```bash
make -C build check        # the pre-push gate: warning-clean build, both suites
make -C build check-full   # the sweep: clang-tidy, bare profiles, generated-source
                           # drift, the OPTA cross build, then the gate under ASan+UBSan
make -C build coverage     # per-area line coverage, reported not enforced
```

The library compiles `-Werror`, so a warning in a library source fails `check`
at the build step. clang-tidy and the sanitizers are in `check-full` rather
than `check`: both are too slow to pay per edit.

```bash
make -C build botanik_tests             # just build the doctest binary
./build/tests/botanik_tests             # run it directly (doctest CLI)
./build/tests/botanik_tests -tc=<name>  # one case
ctest --test-dir build --output-on-failure
```

## Licence

Botanik is free software under the GNU General Public License, version 3 - see
[LICENSE](LICENSE).

`vendor/matiec` is a separate work under the same licence, with its own
`COPYING` and an `UPSTREAM` note recording the commit it came from. It is a
build tool: we run the binary and commit its output, and none of it is linked
or compiled into the firmware. Its IEC library would have been the exception,
which is why `src/libbotanik/plc/functions/ieclib.txt` replaces it and why
`vendor/matiec` carries no `lib/`.

## Links

- [CONTRIBUTING.md](CONTRIBUTING.md) - code style and the non-blocking rules.
- [CLAUDE.md](CLAUDE.md) - orientation for AI coding agents.
- API reference: `make -C build docs`, then open `docs/doxygen/html/index.html`.
- [Arduino OPTA](https://www.arduino.cc/pro/hardware/product/opta/)
- [Modbus Application Protocol](https://modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf)
