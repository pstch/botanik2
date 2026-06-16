# Event Dispatch Architecture

This document describes how input events flow from the hardware serial port to
the active terminal page. For the pages themselves see
[PAGES.md](PAGES.md); for the output side (log entries flowing *out* to the
pages and serial sinks) see [LOG_FLOW.md](LOG_FLOW.md).

## Layers

### BufferedSerial

The lowest layer. @ref Botanik::Utils::BufferedSerial wraps the hardware UART
with a 256-byte RX ring buffer and a 4 KB TX ring buffer. All serial reads and
writes go through this component; it never blocks - a full TX ring drops the
tail rather than stalling the loop.

### AnsiTerminal

The central coordinator. @ref Botanik::Terminal::AnsiTerminal owns the
`BufferedSerial` connection and drives all terminal I/O. Each call to
@ref Botanik::Terminal::AnsiTerminal::step "step()":

1. Bootstraps the display on the first call (clear + status row + focus the
   active page), if no `setup_*()` already did.
2. Refreshes the row-0 status bar when its interval has elapsed and the TX ring
   has room.
3. Pulls the next @ref Botanik::Terminal::InputEvent from the parser and
   dispatches it (one event per `step()`).
4. **Ticks every page** - not just the active one - so each can drain its
   queued log entries (see [LOG_FLOW.md](LOG_FLOW.md)).

@ref Botanik::Terminal::InputParser decodes raw bytes into structured
`InputEvent`s, handling X3.64 CSI sequences, Alt-prefixed keys, function-key
escape sequences, and the kitty `CSI u` form. Held BACKSPACE / Ctrl+D presses
are coalesced into one event carrying a repeat `count`.

The @ref Botanik::Terminal::Ansi helper provides a stateless, write-only API for
emitting escape sequences (cursor movement, erase, insert/delete rows, etc.).

### Pages

Each page implements the @ref Botanik::Pages::Page interface. `AnsiTerminal`
holds up to 8 page pointers and routes events to whichever page is currently
active. See [PAGES.md](PAGES.md).

## Dispatch flow

```
                Hardware Serial port
                        |
                        │ raw bytes
                        ▼
 ┌─────────────────────────────────────────────────────┐
 │                  BufferedSerial                     │
 │        RX ring (256 B) / TX ring (4 KB)             │
 └──────────────────────┬──────────────────────────────┘
                        │ raw bytes (drained in step())
                        ▼
 ┌─────────────────────────────────────────────────────┐
 │                  AnsiTerminal::step()               │
 │                                                     │
 │  ┌────────────────────────────────────────────┐     │
 │  │  InputParser.parse()  →  InputEvent        │     │
 │  └──────────────────────┬─────────────────────┘     │
 │                         │                           │
 │            ┌────────────┴────────────┐              │
 │       CONTENT event           COMMAND event         │
 │            │                         │              │
 │            │            ┌────────────┴───────────┐  │
 │            │            │  Terminal intercepts   │  │
 │            │            │  (consumed here):      │  │
 │            │            │   CTRL_L → redraw      │  │
 │            │            │   HOME   → switch_to(0)│  │
 │            │            │   ALT_1…ALT_9 / ALT_0  │  │
 │            │            │   TAB / Ctrl+PgUp/PgDn │  │
 │            │            └────────────┬───────────┘  │
 │            │                         │ (remaining)  │
 └────────────┼─────────────────────────┼──────────────┘
              │                         │
              ▼                         ▼
   page→handle_text_batch(…)    page→handle_command(cmd, count)
              │                         │
              └────────────┬────────────┘
                           ▼
                  Active page (one of):

  ┌──────────────┐ ┌──────────────┐ ┌───────────────────────────┐
  │  ShellPage   │ │  LogsPage    │ │ HomePage / StatusPage     │
  │              │ │              │ │   (MetricPage subclasses) │
  │ TextEditor   │ │ ENTER →      │ │                           │
  │ CommandShell │ │   focus()    │ │ ENTER → draw_dynamic()    │
  │              │ │ SPACE → mark │ │ other keys ignored        │
  └──────────────┘ └──────────────┘ └───────────────────────────┘

  Then: every page (active + background) gets tick(active)
```

## Terminal-level intercepts

A handful of commands are consumed by
@ref Botanik::Terminal::AnsiTerminal::step "AnsiTerminal::step()" and never reach
the active page - `Ctrl+L` (full redraw), `HOME` / `Alt+1`...`Alt+0` (jump to a
page), and `TAB` / `Ctrl+PageUp`/`PageDown` (cycle pages). Everything else is
forwarded to the active page. See [KEYBINDINGS.md](KEYBINDINGS.md) for the exact
bindings, including their effects.
