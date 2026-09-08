# Contributing to Botanik

## Code style

### Include guards

Every header uses an `#ifndef` guard named after the file's path relative to
`src/`, with `/` and `.` replaced by `_` and everything uppercased:

```
src/libbotanik/utils/serial.h  ->  UTILS_SERIAL_H
src/libbotanik/console/ansi.h  ->  CONSOLE_ANSI_H
src/libbotanik_config.h        ->  LIBBOTANIK_CONFIG_H
```

```cpp
#ifndef SUBSYSTEM_FILENAME_H
#define SUBSYSTEM_FILENAME_H

// ... content ...

#endif // SUBSYSTEM_FILENAME_H
```

One space before the `//` on the `#endif`.

### Naming conventions

| Entity | Convention | Example |
|---|---|---|
| Namespaces | PascalCase | `Botanik`, `Terminal`, `PlcIO` |
| Classes / structs | PascalCase | `BufferedSerial`, `InputEditor` |
| Enum types | PascalCase (`enum class`) or UPPER_SNAKE_CASE prefix (plain `enum`) | `enum class State`, `enum PlcRxState` |
| Enum values | UPPER_SNAKE_CASE | `PLC_RUNNING`, `LEFT` |
| Public methods | `snake_case` | `write_chars()`, `available()` |
| Private methods | `_snake_case` | `_shift_row()`, `_build_rows()` |
| Private members | `_snake_case` | `_serial`, `_rx_head` |
| Public members, locals | `snake_case` | `buf_start`, `row_count` |
| Constants | UPPER_SNAKE_CASE | `RX_BUF_SIZE`, `TASK_PERIOD_MS` |

### Documentation

> Comments say what the code cannot: units, ownership, sentinels, wire facts,
> hardware quirks, invariants. Rationale, alternatives, history and
> architecture go to `docs/`, or are deleted.

A comment that would still be true after the function was rewritten a
different way is rationale, and does not belong next to the code.

Prefer readable code to code that needs a comment. A well-named function or
local beats explaining a block.

#### Headers

Every public entity carries a Doxygen comment, within this budget:

| | Budget |
|---|---|
| `@brief` | 1-2 lines, hard cap 3 |
| `@note` | at most one, <= 2 lines, for a caveat that changes how you call it |
| Whole doc block | <= 6 lines of prose; `@param` and `@return` are contract and do not count |
| `@param` / `@return` | only for a non-obvious unit, range, ownership, sentinel or error value |
| `@code` | not in headers |
| `@section` | not in headers |
| `@file` block | `@file` plus a one-line `@brief`, and at most one `@note` |
| Data members | a one-line trailing `///<`, or nothing |
| `@name` groups | flat, per scope, no prose inside the group |
| `#include` annotations | omit |

There is no prose ratio for a header: the budget above is the rule.

```cpp
/** @brief Write to the TX ring.
 *  @return bytes accepted; short of @p len when the ring is full. */
uint16_t write(const char* buf, uint16_t len);
```

Three tags that only restate the signature are worse than none:

```cpp
/**
 * @brief Short description.
 * @param buf  Input buffer.
 * @param len  Number of bytes to write.
 * @return Number of bytes actually written.
 */
```

#### Implementations

Comment non-obvious invariants, hardware and protocol quirks, workarounds and
their reason, and intent that is not legible from the code. No comment that
restates the line below it, and no section banners.

Keep prose to 30% of code lines or under. `cloc` gives the ratio when you want
a number; nothing enforces it.

```cpp
// ICH via SM4: write 'pressure' chars in insert mode, opening a gap that
// pushes the existing on-screen row content right; overflow falls off the edge.
_terminal->write_chars(&_buf[pos], pressure, /*insert=*/true);
```

A few blocks run past six lines because they are the only place a contract
lives - the alarm dwell semantics in `vars/declare.h`, the RS-485 DE guard
times in `hal/platform.cpp`, the echo count in `modbus/rtu/framing.h`. Where a
file sits over the ratio, ask which rule above it breaks, not how far over it
is.

#### Tests

The test name is the documentation. No comment header above a `TEST_CASE`, no
prose narrating the steps, no comment restating a `CHECK`. Comment a
non-obvious constant or a real gotcha: why *this* byte sequence, why *this*
timing.

Prefer a shared fixture to repeating setup - `tests/site_bank.h`,
`tests/term_fixture.h`, `tests/live_fixture.h`, `tests/recording_writer.h`.

All test code lives in `namespace tests`, one per-file namespace inside it
(`namespace tests { namespace modbus_map {`). The library declares two
namespaces, `Botanik` and `Site`; without this the suite added forty more
beside them, and a test type in `Botanik` is worse still.

#### Prose style

In comments, docs and commit messages alike: no "not X but Y" constructions,
no history (`git log` has it), no restating a rule three ways for emphasis, no
em-dashes (use ` - `, a colon or parentheses).

---

## Non-blocking design

Nothing in this codebase blocks. On the OPTA, blocking stalls the RTOS and the
watchdog.

### The `step()` contract

Any class that needs regular CPU time exposes:

```cpp
void step();  // called every iteration of the main loop; returns quickly
```

The main loop calls `step()` on every stateful object in a fixed order. Each
call does a bounded, small amount of work and returns. Never wait for I/O,
never spin.

### Bounded loops

Loops inside `step()` are bounded by a compile-time or known worst-case count:

- `BufferedSerial::receive()` / `transmit()` - the UART FIFO depth.
- `BufferedLogWriter::drain()` - `LOG_QUEUE_SIZE`, 64 entries.
- `PlcLogReader::step()` - `LOG_PLC_BUFFER_SIZE`, 32 entries.

A loop that could outrun the task budget (`TASK_PERIOD_MS`, 10 ms) needs a
per-call limit and state carried across calls.

### Silent TX drops

`BufferedSerial::write()` returns the number of bytes written and returns early
when the TX ring is full. The ANSI layer does not check that return value, so a
partial escape sequence can be lost and the display corrupt transiently. If the
baud rate cannot keep up with the render workload, render less often rather than
blocking.

### Logging

`Logger::push_entry()` enqueues to each writer's FIFO and returns. Writers need
`drain()` called from the main loop; without it the queue fills and new entries
are dropped.

## Checking your work

```bash
cmake -B build -DBOTANIK_TARGET=SIMULATOR
make -C build
make -C build check        # the gate
make -C build check-full   # the sweep, before a push that touches memory handling
```

clang-tidy runs incrementally over the TUs whose sources or headers changed:

```bash
make -C build clang-tidy
rm -rf build/clang-tidy && make -C build clang-tidy   # the whole tree
```

Pin **LLVM 19** (`clang-tidy-19`) in the editor, the CLI and CI; findings differ
across major versions. `.vscode/settings.json` points the MS C/C++ extension at
the system binary, since it otherwise uses its own bundled build.

### Sanitizers

SIMULATOR-only, via `BOTANIK_SANITIZE` (`address` | `undefined` |
`address+undefined`), off by default. `make check-full` runs the gate
instrumented in `build/asan/`. To drive one by hand, use a separate build
directory so the flags never mix:

```bash
cmake -B build-asan -DBOTANIK_SANITIZE=address+undefined
make -C build-asan botanik_sim
UBSAN_OPTIONS=print_stacktrace=1 ./build-asan/botanik_sim 2>san.log
```

Findings are recoverable by default, so the interactive simulator stays usable.
For a one-shot run that exits on the first finding:

```bash
ASAN_OPTIONS=halt_on_error=1:detect_leaks=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ./build-asan/botanik_sim 2>san.log
```
