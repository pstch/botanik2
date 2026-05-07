# TODO

## Small work items

 - In the simulator's main.cpp, we should be able to include just "libbotanik.h" to have everything directly available.

 - The wiring code in main() is not complete yet. Also, we'll change the code so that we can get timestamp_ms from scheduler.tick(), and we'll make it look more like an Arduino sketch with separate setup() and loop() functions. 

  - In main(), we should make the PlcEmulator run in a separate thread to emulate the PLC runtime running on the other core of the MCU


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

  In practice, the StatusPage class itself should only provide the "scaffolding" for a status page. Some of the contents of a status page are static and should only be drawed when activating the page or redrawing it, while some contents need to be changed at periodic interval. 

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
