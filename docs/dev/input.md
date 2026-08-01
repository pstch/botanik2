# Input dispatch {#dev_input}

How input events flow from the hardware serial port to the active terminal page. For
the pages themselves see @ref dev_pages; for the output side - log entries flowing
*out* to the pages and serial sinks - see @ref dev_logging. The bytes those pages
produce are paced by the TX path in @ref dev_screen.

## Layers

### BufferedSerial

The lowest layer. @ref Botanik::Utils::BufferedSerial wraps the hardware UART
with a 256-byte RX ring buffer and a 4 KB TX ring buffer. All serial I/O goes
through it; it never blocks - a full TX ring drops the tail rather than stalling
the loop.

### AnsiTerminal + Console

Two layers. @ref Botanik::Terminal::AnsiTerminal is the VT **device**: it owns the
`BufferedSerial` connection, tracks the cursor/attribute/geometry state, and emits ANSI
escape sequences. @ref Botanik::Terminal::Console composes one (exposed via
@ref Botanik::Terminal::Console::terminal "terminal()") and is the **coordinator**: each
@ref Botanik::Terminal::Console::step "step()" bootstraps the display on the first call,
refreshes the row-0 status bar when due and the TX ring has room, dispatches one
@ref Botanik::Terminal::InputEvent from the parser, and **ticks every page** (not just
the active one) so each can drain its queued log entries (see @ref dev_logging).

\startuml "the console layer: Console (a Component) composes the AnsiTerminal device, owns the two bars, and holds the page array"
class Component
class AnsiTerminal
class StatusBar
class AlarmBar
class Page

Component <|-- Console
Console *-- AnsiTerminal : terminal()
Console *-- StatusBar
Console *-- AlarmBar
Console o--> "0..8" Page : routes input to the active one
StatusBar ..> AnsiTerminal : paints via
AlarmBar ..> AnsiTerminal : paints via
\enduml

@ref Botanik::Terminal::InputParser decodes raw bytes into structured `InputEvent`s,
handling X3.64 CSI sequences, Alt-prefixed keys, function-key escapes, and the kitty
`CSI u` form. Held BACKSPACE / Ctrl+D presses coalesce into one event carrying a repeat
`count`. The @ref Botanik::Terminal::Ansi helper is a stateless write-only API for
emitting escape sequences (used by `AnsiTerminal`).

One decoded event is an @ref Botanik::Terminal::InputEvent. Its `type` selects which
half of the payload is live: `CONTENT` uses the `batch_data`/`batch_len` run, `COMMAND`
uses the `command` key (plus its `count`). The CSI fields (`params`/`param_count`/
`prefix`/`value`) carry a partially-parsed escape sequence.

\startuml "InputEvent: one decoded key or text run; `type` selects the live half"
class InputEvent {
  type : EventType - NONE / CONTENT / COMMAND
  command : Command - decoded key (COMMAND)
  count : uint16 - repeat for coalesced keys (default 1)
  batch_data : const char* - printable run (CONTENT)
  batch_len : uint16 - run length
  params : uint16[8] - CSI numeric params
  param_count : uint8 - CSI params seen
  prefix : char - CSI private prefix (e.g. '?')
  value : uint8 - final decoded byte
}
\enduml

### Pages

@ref Botanik::Terminal::Console holds up to 8 @ref Botanik::Pages::Page pointers and
routes events to the active one. See @ref dev_pages.

## Dispatch flow

```
 hardware serial port
        │ raw bytes
        ▼
 BufferedSerial  (RX ring 256 B | TX ring 4 KB)
        │ drained in step()
        ▼
 InputParser.parse()  →  InputEvent
        ├───────────────────────────┐
        ▼                           ▼
 CONTENT (printable run)     COMMAND (key + repeat count)
        │                           │
        │                           ▼
        │                  Console intercepts (consumed):
        │                  Ctrl+L, HOME, Alt+1..8,
        │                  TAB / Ctrl+PgUp/PgDn, Ctrl+C
        │                           │ remaining
        ▼                           ▼
 handle_text_batch()        handle_command(cmd, count)
        └─────────────┬─────────────┘
                      ▼  dispatched to the active page:
   ┌───────────┬──────┴─────┬────────────┐
   ▼           ▼            ▼            ▼
 ShellPage   LogsPage   AlarmsPage    HomePage ...
   │ ShellPage
   ▼
 InputEditor (TextEditor):  CONTENT → insert,  COMMAND → move/delete
```

## Console-level intercepts

A handful of commands are consumed by
@ref Botanik::Terminal::Console::step "Console::step()" and never reach
the active page (redraw, page switching); everything else is forwarded. `Ctrl+C` is
the one hybrid: the console offers it to the active page first via
@ref Botanik::Pages::Page::handle_cancel "handle_cancel()", and only if the page
declines applies the default - quit on the simulator, a **no-op on hardware**
(a stray Ctrl+C must never reboot the controller). The full intercept list is the
"Console intercepts" table in @ref user_console.
