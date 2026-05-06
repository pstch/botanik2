# Contributing to Botanik

## Code style

### Include guards

Every header uses a traditional `#ifndef` include guard. The macro name is
derived from the file's path relative to `src/`, replacing `/` and `.` with `_`
and uppercasing everything:

```
src/libbotanik/utils/serial.h  →  UTILS_SERIAL_H
src/libbotanik/terminal/ansi.h →  TERMINAL_ANSI_H
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

**Headers** — all public members carry Doxygen-style doc comments:

```cpp
/**
 * @brief Short description.
 * @param buf  Input buffer.
 * @param len  Number of bytes to write.
 * @return Number of bytes actually written.
 */
uint16_t write(const char* buf, uint16_t len);
```

Use `/** @brief ... */` for one-liners and `@param` / `@return` / `@note` when
the signature alone is insufficient. Group related members with `@name` / `@{`
/ `@}` Doxygen groups.

**Implementations** — inline comments explain *why*, not *what*. A comment that
just restates the code adds no value and should be omitted. Reserve comments for
non-obvious invariants, hardware quirks, or algorithmic intent:

```cpp
// ICH via SM4: write 'pressure' chars in insert mode, opening a gap that
// pushes the existing on-screen row content right; overflow falls off the edge.
_terminal->write_chars(&_buf[pos], pressure, /*insert=*/true);
```

### Internal-only helpers

Implementation details that must appear in a header (e.g., `constexpr` helpers
needed by `static_assert`) go in a `detail` sub-namespace and are not part of
the public API:

```cpp
namespace detail {
    constexpr bool is_power_of_two(uint16_t n) { ... }
}
```

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
`step()` must do a bounded, small amount of work and return promptly — never
wait for I/O, never spin.

### Bounded loops

Loops inside `step()` are always bounded by a compile-time or known-worst-case
count:

- `BufferedSerial::receive()` / `transmit()` — bounded by UART FIFO depth.
- `BufferedLogWriter::drain()` — bounded by `LOG_QUEUE_SIZE` (64 entries max).
- `PlcLogReader::step()` — bounded by `LOG_PLC_BUFFER_SIZE` (32 entries max).

If a loop could potentially run for longer than the task budget
(`TASK_PERIOD_MS` = 10 ms), add a per-call limit and carry state across calls.

### Silent TX drops

`BufferedSerial::write()` returns the number of bytes actually written. When the
TX ring buffer is full it returns early — **it never blocks**. Callers in the
ANSI layer (`ansi.cpp`) do not check the return value by design; the consequence
is that partial escape sequences can be lost, causing transient display
corruption. This is an accepted trade-off. If the baud rate is too slow for the
rendering workload, reduce render frequency rather than adding blocking.

### Logging

`Logger::push_entry()` enqueues to each registered writer's internal FIFO and
returns immediately. Writers must have their `drain()` called from the main loop
(e.g., via `SerialLogWriter`). If `drain()` is never called, the queue fills and
new entries are silently dropped.

---

## Loose coupling

- Classes communicate through narrow interfaces (pointers or references to
  abstract base classes or HAL types), never through global state.
- The HAL (`hal/platform.h`) is the only platform-specific seam. All other code
  calls `HAL::current_ms()`, `HAL::dsb()`, etc., and is otherwise portable
  between SIMULATOR and OPTA builds.
- Panes receive an `AnsiTerminal*` pointer; they do not hold a reference to the
  serial port directly.

---

## Checking your work

```bash
# Build the simulator (the only target that works without Arduino PLC IDE headers)
cmake -B build -DBOTANIK_TARGET=SIMULATOR
make -C build

# Static analysis (runs automatically as part of the build in SIMULATOR mode)
make -C build cppcheck
```

There are no automated tests yet. Manual verification against the simulator is
the current acceptance criterion.
