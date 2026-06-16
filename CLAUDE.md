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

OPTA builds use `arm-none-eabi-gcc/g++` and require Arduino MBED headers under `includes/`.

There are no tests yet.

## Architecture

The system is split into six subsystems under `src/libbotanik/`:

**HAL (`hal/`)** - `platform.h` defines the unified interface (`current_ms()`, `uptime_ms()`, `dsb()`, `ordered_copy/move`). `simulator.h` implements it with `std::chrono` and Linux serial I/O; `opta.h` implements it for Mbed OS. All other code uses HAL exclusively - never raw `<time.h>` or platform-specific calls.

**Terminal UI (`terminal/`)** - `AnsiTerminal` owns the serial connection and drives a multiplexed page display. `InputParser` decodes raw byte streams into `InputEvent`s (handles X3.64 CSI, Alt, and function-key escape sequences). `InputEditor` is a line-editing state machine that consumes `InputEvent`s. `Ansi` is a stateless helper that emits ANSI escape codes.

**Pages (`pages/`)** - Each page (`Home`, `Shell`, `Logs`, `Status`) implements the `Page` interface. `AnsiTerminal` routes input events to the active page and calls each page's render method when the display needs updating.

**Logging (`logs/`)** - `Logger` uses a dual-bucket token-bucket rate limiter (one global bucket, one per topic) before forwarding to registered `LogWriter`s. Writers include `BufferedLogWriter` and `SerialLogWriter`. Log topics are defined in `format.h` (`BOOT`, `NETWORK`, `TIME`, `STATUS`, `PLC_IO`).

**PLC I/O (`plcio/`)** - `PlcSyncHandler` manages two independent state machines (RX and TX) for shared-memory communication with the PLC coprocessor. Uses explicit ARM DSB barriers (`HAL::dsb()`) and transactional read/write patterns to prevent torn operations. Timing constants: heartbeat 1 s, timeout 5 s, watchdog 30 s.

**Utils (`utils/`)** - `BufferedSerial` wraps UART with a 256-byte RX ring buffer and 4096-byte TX buffer. `time.h` provides uptime tracking on top of HAL.


### Key compile-time defines

| Define | Set when |
|---|---|
| `TARGET_SIMULATOR` | SIMULATOR build |
| `DEBUG` | SIMULATOR build |
| `_GNU_SOURCE` | SIMULATOR build |
| `TARGET_OPTA` | OPTA build |
| `ARDUINO` / `MBED_*` | OPTA build |
