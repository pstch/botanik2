# TODO

## Small work items

 - The wiring code in main() is not complete yet. Also, we'll change the code so that we can get timestamp_ms from scheduler.tick(), and we'll make it look more like an Arduino sketch with separate setup() and loop() functions. 

  - In main(), we should make the PlcEmulator run in a separate thread to emulate the PLC runtime running on the other core of the MCU


  There are also some other related changes regarding that (the ability to extend terminal pages and the way we read the system configuration data from the PLC output). Note that site-specific configuration data should go in PlcInputData_t and PlcOutputData_t, while system-level configuration should go in PlcInput_t and PlcOutput_t. The layout of both these structures will need to match the layout used by the PLC runtime running on the other core of the MCU.

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

NOTE: all screen interactions done by Page classes should always go through AnsiTerminal, so that cursor movement and hardware scroll events can be properly tracked. We can hardcode in pages code that row 0 is reserved for the terminal's status line. Note that row 0 here corresponds to row 1 on the terminal, since we target the Minitel 1B were the actual row 0 is not accessible. 

- **`ShellPage`** (`pages/shell.h/cpp`): Implement a proper interactive shell —
  command history (up/down arrow), cursor movement within the input line, and
  integrated log output that interleaves with command I/O. Should be extensible by subclassing so that the user can add commands related
  to their business logic. Scroll-back will be implemented later, after we are able to run the first simulations of the ANSI rendering code in a VT100 emulator, to ensure we have a working X3.64 terminal implementation before adding complex features such as scrollback.

- **`LogsPage`** (`pages/logs.h/cpp`): The current implementation is a draft. Add
  scroll-back, filtering by level/topic, and a proper status line showing drop counts.

- **`HomePage`** (`pages/home.h/cpp`): ASCII logo, current PLC status, and the key
  business-logic metrics (next irrigation event, ventilation state, etc.).

  Should be extensible by subclassing : the base class will handle displaying a menu to reach other pages, the ASCII logo, system status information (like AC supply status, DC system voltage, UPS status, uptime, active fault count, etc) and will provide hooks so that derived classes can expose other information. The base class should also provide a small region at the bottom (last 4 rows) to show the last logs.

- **`StatusPage`** (`pages/status.h/cpp`): Detailed runtime metrics — PLC link status,
  CPU load and stack usage graphs, Ethernet/NTP status, uptime.

  In practice, the StatusPage class itself should only provide the "scaffolding" for a status page. Some of the contents of a status page are static and should only be drawed when activating the page or redrawing it, while some contents need to be changed at periodic interval. 

## Logging

- **Emit suppressed-entry count** (`logs/logger.cpp`): In `fill_buckets()`, emit a
  synthetic log entry for each bucket whose `suppressed` counter is non-zero, then
  reset the counter so callers know messages were dropped.

## Testing

- **Unit tests**: No tests exist. The following components are self-contained enough
  to test without hardware: `LoopScheduler` (tick/skip-beats), `Logger` token-bucket,
  `_project_rollover` (rollover arithmetic), `InputParser` (state machine), and
  `InputEditor` (geometry / ripple algorithm).
