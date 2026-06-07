# botanik // small-scale horticultural greenhouse control system

![Botanik logo](images/logo.png "Botanik")

This project hosts the programming of a control system for a small-scale
horticultural greenhouse. It handles irrigation (on fixed schedules) and
ventilation (air blowers, intractors, window openers, thermal screens and an
evapo-transpiration cooling system). It runs alongside other autonomous
systems — an emergency heat blower and a heatpump, each on their own
thermostat — and monitors their operation, the power supply, and the emergency
generator.

The system runs on an Arduino OPTA PLC (STM32H747 dual-core ARM) with a few
Modbus RTU I/O modules, which lets us run custom C++ alongside the PLC
programming. That custom code exists to work around the many shortcomings of
Arduino's PLC platform — which is frustrating, largely undocumented, and
missing features standard elsewhere. This repository is therefore also a place
to capture the knowledge we've gathered about that platform.

## Library structure

`libbotanik` is a C++11 library, split into independent subsystems under
`src/libbotanik/`. It is platform-agnostic: everything goes through a Hardware
Abstraction Layer so the same code builds as a Linux simulator or for the OPTA.

| Subsystem | Path | Responsibility |
|---|---|---|
| **HAL** | `hal/` | Unified platform interface (clocks, memory ordering, stack/PLC introspection, serial). `simulator.h` and `opta.h` implement it. |
| **Scheduler** | `scheduler/` | Non-blocking fixed-period loop driver with CPU-load and stack-usage metrics. |
| **Terminal UI** | `terminal/` | `AnsiTerminal` drives a multiplexed page display; `InputParser` decodes byte streams into events; `TextEditor` is a byte-efficient line editor. |
| **Pages** | `pages/` | Full-screen views (`Shell`, `Logs`, `Home`, `Status`) behind a common `Page` interface. |
| **Logging** | `logs/` | `Logger` rate-limits with a dual token-bucket, then fans entries out to `LogWriter`s. |
| **PLC I/O** | `plcio/` | `PlcSyncHandler` runs RX/TX state machines over shared memory with the PLC coprocessor; an emulator stands in for it on the simulator. |
| **Utils** | `utils/` | `BufferedSerial` (ring-buffered UART) and time/uptime helpers. |

The application wires these together and runs them from a single non-blocking
loop — see `src/botanik_sim/main.cpp` for the canonical setup (the OPTA `.ino`
sketch does the equivalent).

## Key concepts

- **Nothing blocks.** Every stateful object exposes a `step()` that does a
  small, bounded amount of work and returns immediately; the main loop calls
  them in a fixed order. Blocking would stall the RTOS and trip the watchdog on
  the OPTA. See [CONTRIBUTING.md](CONTRIBUTING.md) for the full contract.
- **HAL-only platform access.** Code never calls `<time.h>` or platform APIs
  directly — only `HAL::current_ms()`, `HAL::dsb()`, `HAL::ordered_copy()`, etc.
  This is what makes the simulator possible.
- **PLC shared memory.** Host and PLC coprocessor communicate through shared
  memory using transactional read/write patterns and explicit ARM DSB barriers
  to avoid torn reads, with heartbeat / timeout / watchdog tracking.
- **Rate-limited logging.** A global bucket plus a per-topic bucket throttle log
  volume; suppressed-message counts are reported on a slower cadence.
- **Byte-efficient terminal.** The UI targets a low-bandwidth serial link, so
  the terminal/editor layer minimizes the escape sequences it emits.

## Building

The default target is the Linux simulator (`SIMULATOR`), which compiles to a
native executable and is the only target that builds end-to-end today:

```bash
cmake -B build -DBOTANIK_TARGET=SIMULATOR
make -C build
./build/botanik_sim        # run the simulator
```

`cppcheck` static analysis runs as part of the simulator build.

### OPTA target

The `OPTA` target cross-compiles for the Arduino OPTA PLC with
`arm-none-eabi-gcc`:

```bash
cmake -B build-opta -DBOTANIK_TARGET=OPTA
make -C build-opta
```

**This does not build out of the box.** It needs a large set of Arduino
MBED / PLC IDE headers under `includes/` that are **not** distributed here; you
obtain them from the Arduino PLC IDE's build cache after a successful build
there and copy them in by hand. Even with those headers a full link is not yet
working, but it is enough to make IntelliSense resolve correctly.

## Documentation & useful links

- [CONTRIBUTING.md](CONTRIBUTING.md) — code style, naming, and the non-blocking design rules.
- [CLAUDE.md](CLAUDE.md) — architecture summary and build notes for AI coding agents.
- [docs/EVENT_DISPATCH.md](docs/EVENT_DISPATCH.md) — terminal input event flow.
- [docs/KEYBINDINGS.md](docs/KEYBINDINGS.md) — terminal key bindings.
- API reference — run `doxygen Doxyfile`, then open `docs/doxygen/html/index.html`.

### External

- [Arduino OPTA](https://www.arduino.cc/pro/hardware/product/opta/)
- [Modbus Application Protocol](https://modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf)
- [ANSI escape codes](https://en.wikipedia.org/wiki/ANSI_escape_code)
