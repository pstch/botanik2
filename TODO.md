# TODO

## Architecture

- **Make `PlcSyncHandler` site-independent** *(requires its own detailed plan)*:
  Currently `PlcInputData_t` and `PlcOutputData_t` contain botanik-specific fields
  (log buffer, read/write indices), which prevents the library from being reused in
  other projects. The refactor involves:
  - Move `logBuffer`/`writeIndex` out of `PlcOutputData_t` and into a botanik-specific
    data struct that is separate from the sync layer.
  - Move `readIndex` out of `PlcInputData_t` similarly.
  - Templatise `PlcSyncHandler<InputData, OutputData>` so the caller supplies the
    payload types; the sync counters (`PlcInputCounters_t`, `PlcOutputCounters_t`)
    stay fixed and are not part of the template.
  - `PlcSyncHandler` still uses `HAL::get_plc_input()` / `HAL::get_plc_output()` for
    the actual memory pointers; that does not change.
  - Update `PlcLogReader`, `PlcLogWriter`, `PlcEmulator`, and `BotanikApp` accordingly.

## PLC I/O

- **`EthernetHandler`**: State machine (stepped from the main loop) that configures
  the board's Ethernet port using network parameters sourced from `PlcOutputData_t`.
  Watches a version/serial number field in the config; when the serial number changes
  it tears down and re-applies the network configuration.

- **NTP client**: State machine that periodically synchronises the RTC using a server
  address sourced from `PlcOutputData_t`. Steps through DNS resolution, UDP exchange,
  and clock adjustment. Re-syncs when the NTP server config serial number changes.

- **Sunrise/sunset calculator**: Derives daily sunrise and sunset times from GPS
  coordinates and date sourced from `PlcOutputData_t`. Exposes the results for use
  by irrigation and lighting scheduling logic.

## Terminal UI

- **Wire terminal input** (`terminal/terminal.cpp`): Implement `AnsiTerminal::step()` —
  call `_parser.parse()` in a loop and dispatch `InputEvent`s to the active page via
  `handle_text()` / `handle_command()`.

- **Implement `switch_to()`** (`terminal/terminal.cpp`): Full page switching with
  lifecycle — deactivate current page, clear screen, call `focus()` on the new page.

- **Implement `BotanikTerminal`** (`terminal/terminal.h`): Define the constructor;
  create and register the four pages (Home, Logs, Shell, Status).

- **Status bar** (`terminal/terminal.cpp`): `AnsiTerminal` should maintain an always-
  visible top row showing the application name, current datetime, CPU load, and stack
  usage. All pages must leave row 0 reserved for this bar.

- **Selection display in `write_chars()`** (`terminal/terminal.cpp`): Track the active
  selection range and apply reverse-video (or colour) to selected characters as they
  are written.

## Editor

- **`InputEditor::select_characters()`** (`terminal/editor.cpp`): Implement selection
  with damage-range calculation and a dummy `replace_characters()` pass over the
  damaged range.

- **`InputEditor::replace_characters()` screen update** (`terminal/editor.cpp`):
  Reflect in-place replacements on the screen (currently mutates the buffer but
  does not redraw).

## Pages

- **`ShellPage`** (`pages/shell.h/cpp`): Implement a proper interactive shell —
  command history (up/down arrow), cursor movement within the input line, and
  integrated log output that interleaves with command I/O.

- **`LogsPage`** (`pages/logs.h/cpp`): The current implementation is a draft. Add
  scroll-back, filtering by level/topic, and a proper status line showing drop counts.

- **`HomePage`** (`pages/home.h/cpp`): ASCII logo, current PLC status, and the key
  business-logic metrics (next irrigation event, ventilation state, etc.).

- **`StatusPage`** (`pages/status.h/cpp`): Detailed runtime metrics — PLC link status,
  CPU load and stack usage graphs, Ethernet/NTP status, uptime.

## Application

- **Write `main.cpp`** (`botanik_sim/main.cpp`): Replace the placeholder with a real
  app loop — allocate shared PLC memory, call `HAL::set_plcio_pointers()`, construct
  `BotanikApp`, then loop `step()`.

- **Complete `BotanikApp`** (`libbotanik.cpp`): Connect `setup()` / `step()` to a
  public entry point; instantiate `BotanikTerminal` once it is ready.

- **Make `BotanikApp` subclassable**: Expose `setup()` and `step()` as public virtual
  methods so an Arduino sketch can derive from `BotanikApp` and inject business logic
  without touching the library. The derived class calls `super::setup()` /
  `super::step()` and adds its own greenhouse-specific code alongside.

## Logging

- **Emit suppressed-entry count** (`logs/logger.cpp`): In `fill_buckets()`, emit a
  synthetic log entry for each bucket whose `suppressed` counter is non-zero, then
  reset the counter so callers know messages were dropped.

## Scheduler / Monitoring

- **Paint the stack each tick**: `LoopScheduler::_measure_stack_usage()` searches for
  `STACK_PAINT_BYTE` (`0xCD`) but the stack is never painted, and `_measure_stack_usage()`
  itself is never called. To measure per-iteration usage: paint the stack region from
  the current SP down to the stack base at the start of each tick (in `tick()`, just
  before returning `true`), then call `_measure_stack_usage()` at the top of the next
  tick (in `_measure_work()`) to capture the high-water mark left by the previous
  iteration's work.

## Testing

- **Unit tests**: No tests exist. The following components are self-contained enough
  to test without hardware: `LoopScheduler` (tick/skip-beats), `Logger` token-bucket,
  `_project_rollover` (rollover arithmetic), `InputParser` (state machine), and
  `InputEditor` (geometry / ripple algorithm).
