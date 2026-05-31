# TODO

## Documentation

Organize the Doxygen documentation. Maybe some of the things documented in markdown files will need to be moved to the Doxygen-based documentation.

## Performance monitoring

 - The performance monitoring code is a bit of a weak spot right now (and we're not terribly confident about our stack usage monitoring code, especially since we appear to always use the same amount of stack).


## Small work items

 - message suppression seems to misbehave, maybe the suppressed message reporting handler is running too frequently

 - when starting up, we want to show information about the hardware and current firmware

 - when starting up, we want the LoopScheduler to report its task period

 - find a better way than a hardcoded bias to paint the free stack without writing over our own return address, the current 4096-byte bias is negligible on the SIMULATOR, but not on our OPTA target

 - simplify the entry point by moving simulation-specific code to its own header
   (this is required because that entry point will serve as the reference documentation for the Arduino sketch)

 - make the scheduler report overruns, also in the case of overruns we want the CPU load calculation to report more than 100% CPU usage

 - reduce the complexity of the command submission path, in :
   + handle_command() (case Command::ENTER)
   + run()

 - find places where we try to do partial writes to the buffered serial device. instead of having this complex code, we'll just rely on a buffer big enough to hold the entire screen contents

  - the "unknown command" handler is too complex, it should do all of its writes through write_lines()

## ANSI terminal

 - in the parsing code, we'd like the ability to batch character removals

 - this one is hard : add support for soft line-wraps (using Shift+Enter) in the command prompt, and make sure these soft line-wraps are properly handled. normally, we've wrote the code so that it properly handles both soft and hard linewraps, but we've never actually tested this code path, and I'm worried that the origin tracking code will misbehave in that particular case

## PLC I/O

- **`NetworkHandler`**: State machine (stepped from the main loop) that configures
  the board's Ethernet port using network parameters sourced from `PlcOutputConfig_t` (added to `PlcOutput_t`). Watches a version/serial number field in the config; when the serial number changes it tears down and re-applies the network configuration. Some more details are available in the comment preceding the declaration in network.h.

- **TimeSyncHandler**: State machine that periodically synchronises the RTC using an NTP server address sourced from `PlcOutputConfig_t` (added to `PlcOutput_t`). Steps through DNS resolution, UDP exchange, and clock adjustment. Re-syncs when the NTP server config serial number changes. Some more details are available in the comment preceding the declaration in network.h

- **Sunrise/sunset calculator**: Derives daily sunrise and sunset times from GPS
  coordinates and date sourced from `PlcOutputData_t`. Exposes the results for use
  by irrigation and lighting scheduling logic.

## Pages

NOTE: all screen interactions done by Page classes should always go through AnsiTerminal, so that cursor movement and hardware scroll events can be properly tracked. We can hardcode in pages code that row 0 is reserved for the terminal's status line. Note that row 0 here corresponds to row 1 on the terminal, since we target the Minitel 1B were the actual row 0 is not accessible. 

- **Shell command history** (`shell/shell.h/cpp`, `pages/shell.cpp`): Implement
  UP/DOWN (Ctrl+P/N) history navigation in `CommandShell`. On first press, save
  the current editor contents to a draft buffer; load entries from the ring buffer;
  restore the draft when navigating back to head. Buffer sizes are in
  `Config::SHELL_HISTORY_SIZE` / `Config::SHELL_HISTORY_ENTRY`.

- **Shell kill ring** (`shell/shell.h/cpp`, `terminal/editor.h/cpp`, `pages/shell.cpp`):
  Implement Ctrl+K (`kill_to_end()`), Ctrl+W (`kill_word_backward()`), and Ctrl+Y
  (`paste_kill()`) in `InputEditor` and `CommandShell`. A single-slot kill buffer of
  `Config::SHELL_KILL_SIZE` bytes is sufficient for now.

- **`HomePage`** (`pages/home.h/cpp`): ASCII logo, current PLC status, and the key
  business-logic metrics (next irrigation event, ventilation state, etc.).

  Should be extensible by subclassing : the base class will handle displaying a menu to reach other pages, the ASCII logo, system status information (like AC supply status, DC system voltage, UPS status, uptime, active fault count, etc) and will provide hooks so that derived classes can expose other information. The base class should also provide a small region at the bottom (last 4 rows) to show the last logs.

- **`StatusPage`** (`pages/status.h/cpp`): Detailed runtime metrics — PLC link status,
  CPU load and stack usage graphs, Ethernet/NTP status, uptime.

  In practice, the StatusPage class itself should also be extensible and only provide the "scaffolding" for a status page. Some of the contents of a status page are static and should only be drawed when activating the page or redrawing it, while some contents need to be changed at periodic interval. 

## Testing

- **Unit tests**: No tests exist. The following components are self-contained enough
  to test without hardware: `LoopScheduler` (tick/skip-beats), `Logger` token-bucket,
  `_project_rollover` (rollover arithmetic), `InputParser` (state machine), and
  `InputEditor` (geometry / ripple algorithm).
