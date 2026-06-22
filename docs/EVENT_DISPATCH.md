# Event Dispatch Architecture

How input events flow from the hardware serial port to the active terminal page.
For the pages themselves see [PAGES.md](PAGES.md); for the output side (log entries
flowing *out* to the pages and serial sinks) see [LOG_FLOW.md](LOG_FLOW.md).

## Layers

### BufferedSerial

The lowest layer. @ref Botanik::Utils::BufferedSerial wraps the hardware UART
with a 256-byte RX ring buffer and a 4 KB TX ring buffer. All serial I/O goes
through it; it never blocks - a full TX ring drops the tail rather than stalling
the loop.

### AnsiTerminal

The central coordinator. @ref Botanik::Terminal::AnsiTerminal owns the
`BufferedSerial` connection and drives all terminal I/O. Each
@ref Botanik::Terminal::AnsiTerminal::step "step()" bootstraps the display on the first
call, refreshes the row-0 status bar when due and the TX ring has room, dispatches one
@ref Botanik::Terminal::InputEvent from the parser, and **ticks every page** (not just
the active one) so each can drain its queued log entries (see [LOG_FLOW.md](LOG_FLOW.md)).

@ref Botanik::Terminal::InputParser decodes raw bytes into structured `InputEvent`s,
handling X3.64 CSI sequences, Alt-prefixed keys, function-key escapes, and the kitty
`CSI u` form. Held BACKSPACE / Ctrl+D presses coalesce into one event carrying a repeat
`count`. The @ref Botanik::Terminal::Ansi helper is a stateless write-only API for
emitting escape sequences.

### Pages

`AnsiTerminal` holds up to 8 @ref Botanik::Pages::Page pointers and routes events to
the active one. See [PAGES.md](PAGES.md).

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
        │                  Terminal intercepts (consumed):
        │                  Ctrl+L, HOME, Alt+1..0,
        │                  TAB / Ctrl+PgUp/PgDn, Ctrl+C
        │                           │ remaining
        ▼                           ▼
 handle_text_batch()        handle_command(cmd, count)
        └─────────────┬─────────────┘
                      ▼  dispatched to the active page:
   ┌───────────┬──────┴─────┬────────────┐
   ▼           ▼            ▼            ▼
 ShellPage   LogsPage     HomePage    StatusPage
   │ ShellPage
   ▼
 InputEditor (TextEditor):  CONTENT → insert,  COMMAND → move/delete
```

## Terminal-level intercepts

A handful of commands are consumed by
@ref Botanik::Terminal::AnsiTerminal::step "AnsiTerminal::step()" and never reach
the active page (redraw, page switching); everything else is forwarded. `Ctrl+C` is
the one hybrid: the terminal offers it to the active page first via
@ref Botanik::Pages::Page::handle_cancel "handle_cancel()", and only if the page
declines applies the default - quit on the simulator, a **no-op on hardware**
(a stray Ctrl+C must never reboot the controller). The full intercept list is the
"Terminal intercepts" table in [KEYBINDINGS.md](KEYBINDINGS.md).
