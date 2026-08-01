# TODO

 - the exit command should print a newline before actually existing

 - Formalize the way we comment and structure our CPP files

##  Test harness

 - Choose between Python and C++ for the test harness and write a basic test case for each of our main components

## Profiling

 - CPU usage is high, especially on the STATUS page (many diffed fields refreshed
   each period). Profile the main loop / per-page render cost and find the hot spots
   (likely the metric diff/_paint and the per-tick redraw passes).
 - Add a profiling build target to CMake (e.g. a `PROFILE` target or option wiring
   `-pg` / frame-pointer-friendly `-O2 -fno-omit-frame-pointer` for `perf`), so the
   simulator can be profiled without hand-editing flags.

## Cooperative TX pacing (follow-ups)

 - Render-before-diff waste: a paced cold/hot resume re-invokes the subclass
   draw_dynamic()/draw_hot() every tick, re-running every field's snprintf/format_float
   even for fields the per-field diff/ordinal then skips. Pacing bounds TX bytes per
   tick but not the CPU render cost (it multiplies by the number of resume ticks).
   Let the resume skip rendering already-drawn fields (e.g. expose the cold ordinal so
   a subclass can guard its render, or restructure so a field renders once per period).
   Ties into the Profiling item above.

 - Three parallel resume state-machines: _static_pending, _dynamic_pending and
   _hot_pending each carry their own ordinal/blocked/active/pending flags with subtly
   different rules. Generalize into one paced-pass abstraction so a new field class or
   cadence tweak can't desync one copy from the others.

 - Two separate paced painters: Console::step()'s status bar has its own budget +
   round-robin cursor + tx_below_cold() gate, duplicating MetricPage's per-pass
   field-budget machinery. Unify so the cooperative-TX policy (floor, caps, all-or-
   nothing) lives in one place instead of two that must be kept in sync by hand.

## Big work items

- ~~Refactor around LoopScheduler/PerfMonitor, they are weirdly coupled.~~ Done:
  `PerfMonitor` is gone — the per-beat metrics are all tied to the loop timing the
  scheduler owns, so the measurement folded back into `LoopScheduler` (jitter, CPU
  duty cycle `(period − slept)/period`, work duration, stack usage), all published
  into `SchedulerContext`. The genuinely reusable bits — the EMA / windowed-max
  filters and the `PerfContext` POD — were extracted to `utils/filters.h` as a slim,
  unit-testable layer; `SchedulerContext : PerfContext` stays the single struct
  consumers read.
- ~~Decouple the terminal code (status bar & `stats` command) from the separate
  SchedulerContext/PlcSyncContext pointers.~~ Done: `SystemContext` (context.h)
  bundles both; Console takes one `SystemContext` and exposes it via
  `context()`. Consumers read the concrete sub-context fields through it.
  Decided *not* to hide those fields behind a status-provider interface: single
  producer/consumer, POD snapshots are idiomatic here, and the vtable indirection
  isn't worth it on the OPTA. The wrapper already removed the coupling that mattered.

## PLC I/O

- **`NetworkHandler`**: State machine (stepped from the main loop) that configures
  the board's Ethernet port using network parameters sourced from `PlcOutputConfig_t` (added to `PlcOutput_t`). Watches a version/serial number field in the config; when the serial number changes it tears down and re-applies the network configuration. Some more details are available in the comment preceding the declaration in network.h.

- **TimeSyncHandler**: State machine that periodically synchronises the RTC using an NTP server address sourced from `PlcOutputConfig_t` (added to `PlcOutput_t`). Steps through DNS resolution, UDP exchange, and clock adjustment. Re-syncs when the NTP server config serial number changes. Some more details are available in the comment preceding the declaration in network.h

- **Sunrise/sunset calculator**: Derives daily sunrise and sunset times from GPS
  coordinates and date sourced from `PlcOutputData_t`. Exposes the results for use
  by irrigation and lighting scheduling logic.

- **MQTT publisher**: Layered on top of `NetworkHandler` (and the other components),
  a state machine (stepped from the main loop) that cyclically publishes all metrics
  to subscribers. Broker/connection parameters sourced from `PlcOutputConfig_t`;
  re-applies on config serial number change like the other network handlers.

## Pages

NOTE: all screen interactions done by Page classes should always go through AnsiTerminal, so that cursor movement and hardware scroll events can be properly tracked. We can hardcode in pages code that row 0 is reserved for the terminal's status line. Note that row 0 here corresponds to row 1 on the terminal, since we target the Minitel 1B were the actual row 0 is not accessible. 

- **Shell command history** (`shell/shell.h/cpp`, `pages/shell.cpp`): Implement
  UP/DOWN (Ctrl+P/N) history navigation in `CommandShell`. On first press, save
  the current editor contents to a draft buffer; load entries from the ring buffer;
  restore the draft when navigating back to head. Buffer sizes are in
  `Config::SHELL_HISTORY_SIZE` / `Config::SHELL_HISTORY_ENTRY`.

- **Shell kill ring** (`shell/shell.h/cpp`, `console/editor.h/cpp`, `pages/shell.cpp`):
  Implement Ctrl+K (`kill_to_end()`), Ctrl+W (`kill_word_backward()`), and Ctrl+Y
  (`paste_kill()`) in `InputEditor` and `CommandShell`. A single-slot kill buffer of
  `Config::SHELL_KILL_SIZE` bytes is sufficient for now.

- **`HomePage`** (`pages/home.h/cpp`): ASCII logo, current PLC status, and the key
  business-logic metrics (next irrigation event, ventilation state, etc.).

  Should be extensible by subclassing : the base class will handle displaying a menu to reach other pages, the ASCII logo, system status information (like AC supply status, DC system voltage, UPS status, uptime, active fault count, etc) and will provide hooks so that derived classes can expose other information. The base class should also provide a small region at the bottom (last 4 rows) to show the last logs.

- **`StatusPage`** (`pages/status.h/cpp`): Detailed runtime metrics - PLC link status,
  CPU load and stack usage graphs, Ethernet/NTP status, uptime.

  In practice, the StatusPage class itself should also be extensible and only provide the "scaffolding" for a status page. Some of the contents of a status page are static and should only be drawed when activating the page or redrawing it, while some contents need to be changed at periodic interval. 

## Documentation

- **Refresh `docs/KEYBINDINGS.md` Shell page table** once the `[CMD-TABLE]` shell
  command-table refactor lands (`shell/shell.cpp`, `pages/shell.cpp`). The table
  still describes the old `handle_command` if/else dispatch; re-check the key ->
  handler mapping against the new command table.

- **HealthMonitor / fault aggregation** (review `[ARCH-HEALTH]`): today the only
  health signal is `PlcSyncContext::faulted()`, consumed at one place (the watchdog
  kick). Add a small fault-state aggregator that subsystems report named faults
  into (set/clear bits), read by the Status page and (future) the TCP fault-alert
  sender, so the watchdog has a principled "are we healthy" input instead of a
  single hardcoded `faulted()`.

- **Reduce libc dependency** (review `[TODO-LIBC]`): audit our use of libc
  (`snprintf`/`vsnprintf`, `strncmp`, `strlen`, `memcpy`, `strstr`, `strtol`) and
  shrink the surface where practical - prefer the in-house `utils/text` helpers and
  bounded equivalents, so the firmware leans on as little of the C library as
  possible. (No known bugs; the hand-rolled parsing is already bounds-checked.)

- **Repair the HAL porting spec** (`docs/porting.dox:36-45`): it documents a
  pre-namespace HAL (`wall_clock_ms`, `wall_clock_set`, `get_plc_input`,
  `get_plc_output`, `debug_break`, `watchdog_start`, `get_stack_base`) - none of
  those names exist. As written the page cannot be used to port the HAL.

- **`Memory::ordered_move` is documented but does not exist.** Named in
  `CLAUDE.md`, `docs/porting.dox` and `docs/CONCURRENCY.md` as part of the HAL
  interface; there is no such function. Either add it or drop it from all three.

- **Docs still describe the removed PLC interface.** `docs/CONTEXTS.md` (the
  `PlcSyncContext` class + its `SystemContext` edge), `docs/LOG_FLOW.md`
  (`PlcLogReader`, "the `LogTopic` enum in `plcio/vars.h`"), `docs/CONCURRENCY.md`
  (steps 3-4 of the loop order), `docs/library.dox` (the `plcio/` subsystem row
  and the whole `BOTANIK_FEATURE_PLC` section - that flag no longer exists).

- **CLAUDE.md drift.** Documents `AlarmReader<OutputData>` and a remote/local
  bitset merge (neither exists), `solar_elevation()` (it is `solar_position()`,
  also wrong in `docs/library.dox`), and says `HAL::Storage` is "one-file-per-key
  on the sim" when the backend is a single `key=hex` file.

- **Telemetry topic shape.** `vars/access.h` documents
  `<prefix>/telemetry/<topic>/<name>`; `network/telemetry.cpp` builds
  `<prefix>/<MQTT_TELEMETRY_TOPIC>/<group>/<name>`, where the config default is
  `"status"`.
