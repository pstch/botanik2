# Contributing to Botanik

## Code style

### Include guards

Every header uses a traditional `#ifndef` include guard. The macro name is
derived from the file's path relative to `src/`, replacing `/` and `.` with `_`
and uppercasing everything:

```
src/libbotanik/utils/serial.h  →  UTILS_SERIAL_H
src/libbotanik/console/ansi.h →  CONSOLE_ANSI_H
src/libbotanik_config.h        →  LIBBOTANIK_CONFIG_H
```

Structure:

```cpp
#ifndef SUBSYSTEM_FILENAME_H
#define SUBSYSTEM_FILENAME_H

// ... content ...

#endif // SUBSYSTEM_FILENAME_H
```

The `#endif` comment uses exactly one space before `//`.

### Naming conventions

| Entity | Convention | Example |
|---|---|---|
| Namespaces | PascalCase | `Botanik`, `Terminal`, `PlcIO` |
| Classes / structs | PascalCase | `BufferedSerial`, `InputEditor` |
| Enum types | PascalCase (`enum class`) or UPPER_SNAKE_CASE prefix (plain `enum`) | `enum class State`, `enum PlcRxState` |
| Enum values | UPPER_SNAKE_CASE | `PLC_RUNNING`, `LEFT` |
| Public methods | `snake_case` | `write_chars()`, `available()` |
| Private methods | `_snake_case` (leading underscore) | `_shift_row()`, `_build_rows()` |
| Private members | `_snake_case` (leading underscore) | `_serial`, `_rx_head` |
| Public members / local variables | `snake_case` | `buf_start`, `row_count` |
| Constants (`constexpr` / `static const`) | UPPER_SNAKE_CASE | `RX_BUF_SIZE`, `TASK_PERIOD_MS` |

### Documentation

> **Comments say what the code cannot: units, ownership, sentinels, wire facts,
> hardware quirks, invariants. Everything else - rationale, alternatives
> considered, history, architecture - goes to `docs/dev/`, or is deleted.**

Test: if a comment would still be true after the function was rewritten a
different way, it is rationale. It does not belong next to the code.

Prefer readable code to code that needs a comment. Extracting a well-named
function or local beats explaining a block.

#### Headers

Every public member carries a Doxygen comment. Keep it to budget:

| | Budget |
|---|---|
| `@brief` | 1 line, 2 if needed. **Hard cap 3.** |
| `@note` | at most one, <= 2 lines, for a caveat that changes how you call it |
| Whole doc block on one entity | **<= 6 lines** |
| `@param` / `@return` | **only** for a non-obvious unit, range, ownership, sentinel or error value - never to restate the name |
| `@code` examples | **not in headers** - put them in `docs/dev/` or a test |
| `@section` | **not in headers** - that is a doc, by definition |
| `@file` block | `@file` + one-line `@brief`, plus at most one `@note` linking its doc |
| Data members | one-line trailing `///<`, or nothing |
| `@name` groups | keep (flat, per scope) - but no prose inside the group |
| `#include` annotations | omit (`#include <cstdint> // uint16_t/uint32_t`) |

**Per-file budget: comment lines <= 25% of code lines.**

```cpp
/** @brief Write to the TX ring.
 *  @return bytes accepted; short of @p len when the ring is full. */
uint16_t write(const char* buf, uint16_t len);
```

Not this - three tags that say only what the signature already says:

```cpp
/**
 * @brief Short description.
 * @param buf  Input buffer.
 * @param len  Number of bytes to write.
 * @return Number of bytes actually written.
 */
```

#### Implementations

Comment only non-obvious invariants, hardware or protocol quirks, workarounds
(with the reason), and algorithmic intent that is not legible from the code. No
comment that restates the line below it, and no section banners
(`// ---- helpers ----`).

**Per-file budget: comment lines <= 15% of code lines.**

```cpp
// ICH via SM4: write 'pressure' chars in insert mode, opening a gap that
// pushes the existing on-screen row content right; overflow falls off the edge.
_terminal->write_chars(&_buf[pos], pressure, /*insert=*/true);
```

#### Tests

The test name is the documentation. No comment header above a `TEST_CASE`, no
prose narrating the steps, no comment restating a `CHECK`. Comment only a
non-obvious magic constant or a genuine gotcha - why *this* byte sequence, why
*this* timing.

Prefer a shared fixture over repeating (and re-explaining) setup:
`tests/site_bank.h`, `tests/params_fixture.h`, `tests/term_fixture.h`,
`tests/recording_writer.h`.

#### Prose style

Applies to comments, docs and commit messages alike: no "not X but Y"
constructions - state the fact; no history in code, `git log` has it; no
restating a rule three ways for emphasis; no em-dashes (use ` - `, a colon, or
parentheses).

`scripts/comment_budget.py` reports the ratios and flags files over budget; it
runs as part of `make check-full`.

---

## Non-blocking design

**Nothing in this codebase ever blocks.** This is a hard requirement on the OPTA
target, where blocking stalls the RTOS and watchdog.

### The `step()` contract

Any class that needs regular CPU time exposes a `step()` method:

```cpp
void step();  // called every iteration of the main loop; returns quickly
```

The main loop calls `step()` on every stateful object in a fixed order. Each
`step()` must do a bounded, small amount of work and return promptly - never
wait for I/O, never spin.

### Bounded loops

Loops inside `step()` are always bounded by a compile-time or known-worst-case
count:

- `BufferedSerial::receive()` / `transmit()` - bounded by UART FIFO depth.
- `BufferedLogWriter::drain()` - bounded by `LOG_QUEUE_SIZE` (64 entries max).
- `PlcLogReader::step()` - bounded by `LOG_PLC_BUFFER_SIZE` (32 entries max).

If a loop could potentially run for longer than the task budget
(`TASK_PERIOD_MS` = 10 ms), add a per-call limit and carry state across calls.

### Silent TX drops

`BufferedSerial::write()` returns the number of bytes actually written. When the
TX ring buffer is full it returns early - **it never blocks**. Callers in the
ANSI layer (`ansi.cpp`) do not check the return value by design; the consequence
is that partial escape sequences can be lost, causing transient display
corruption. This is an accepted trade-off. If the baud rate is too slow for the
rendering workload, reduce render frequency rather than adding blocking.

### Logging

`Logger::push_entry()` enqueues to each registered writer's internal FIFO and
returns immediately. Writers must have their `drain()` called from the main loop
(e.g., via `SerialLogWriter`). If `drain()` is never called, the queue fills and
new entries are silently dropped.

## Checking your work

```bash
# Build the simulator (the only target that works without Arduino PLC IDE headers)
cmake -B build -DBOTANIK_TARGET=SIMULATOR
make -C build

# clang-tidy (uses .clang-tidy + build/compile_commands.json). Incremental: only
# the TUs whose sources or headers changed since the last run.
make -C build clang-tidy

# ...and the unconditional sweep: drop the stamps, then re-run
rm -rf build/clang-tidy && make -C build clang-tidy
```

### clang-tidy version

Pin **LLVM 19** (`clang-tidy-19`) everywhere - editor, CLI, and CI; findings
differ across major versions. The MS C/C++ extension defaults to its own bundled
clang-tidy, so `.vscode/settings.json` pins it to the system binary
(`clangTidy.path`) and defers all check selection to `.clang-tidy`.

### Sanitizers (ASAN/UBSAN)

SIMULATOR-only runtime sanitizers, via the `BOTANIK_SANITIZE` option
(`address` | `undefined` | `address+undefined`). **Off by default** - they more
than double compile CPU, so they are a sweep rather than a per-edit tax.

The usual way to run them is `make -C build check-full`, which re-runs the whole
gate instrumented in a `build/asan/` tree of its own. To drive one by hand, use a
separate build dir so the flags never mix with the normal build:

```bash
cmake -B build-asan -DBOTANIK_SANITIZE=address+undefined
make -C build-asan botanik_sim

# Interactive: run, exercise the UI, then Ctrl+C for a clean exit (so leak and
# UB checks run at exit). Send sanitizer output to a file to keep the UI readable.
UBSAN_OPTIONS=print_stacktrace=1 ./build-asan/botanik_sim 2>san.log
```

Findings are **recoverable** by default - UBSAN logs each one and execution
continues, so the simulator stays usable. For a strict one-shot/CI run that exits
non-zero on the first finding, add `halt_on_error=1`:

```bash
ASAN_OPTIONS=halt_on_error=1:detect_leaks=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ./build-asan/botanik_sim 2>san.log
```
