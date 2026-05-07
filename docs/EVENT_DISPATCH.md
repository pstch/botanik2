# Event Dispatch Architecture

This document describes how input events flow from the hardware serial port to
the active terminal page.

## Layers

### BufferedSerial

The lowest layer. Wraps the hardware UART with a 256-byte RX ring buffer and a
1 KB TX buffer. All serial reads and writes go through this component.

### AnsiTerminal

The central coordinator. It owns the `BufferedSerial` connection and drives
all terminal I/O. Each call to `step()` drains a batch of raw bytes from the
serial buffer, feeds them through `InputParser`, and dispatches the resulting
`InputEvent`s to the active page — after intercepting a small set of
terminal-level commands itself.

`InputParser` decodes raw bytes into structured `InputEvent`s, handling X3.64
CSI sequences, Alt-prefixed keys, and function-key escape sequences.

The `Ansi` helper provides a stateless, write-only API for emitting escape
sequences (cursor movement, erase, insert/delete rows, etc.).

### Pages

Each page (`ShellPage`, `LogsPage`, `HomePage`, `StatusPage`) implements the
`Page` interface. `AnsiTerminal` holds up to 8 page pointers and routes events
to whichever page is currently active.

## Dispatch flow

```
Hardware Serial port
      │ raw bytes
      ▼
┌──────────────────────────────────────────────────┐
│                  BufferedSerial                   │
│         RX ring (256 B) / TX buffer (1 KB)        │
└──────────────────────┬───────────────────────────┘
                       │ raw bytes (drained in step())
                       ▼
┌──────────────────────────────────────────────────┐
│                  AnsiTerminal                     │
│                                                   │
│  ┌────────────────────────────────────────────┐  │
│  │  InputParser.parse()  →  InputEvent        │  │
│  └──────────────────────┬─────────────────────┘  │
│                         │                         │
│            ┌────────────┴────────────┐            │
│       CONTENT event           COMMAND event        │
│            │                         │            │
│            │        ┌────────────────┤            │
│            │        │  Intercepted   │            │
│            │        │  by terminal:  │            │
│            │        │  CTRL_L        │            │
│            │        │  HOME          │            │
│            │        │  ALT_1…ALT_9   │            │
│            │        │  ALT_0         │            │
│            │        └────────────────┘            │
│            │               │ (remaining)           │
└────────────┼───────────────┼──────────────────────┘
             │               │
             ▼               ▼
    page→handle_text(c)    page→handle_command(cmd)

                  Active page (one of):

    ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐
    │    ShellPage     │  │    LogsPage      │  │ HomePage /       │
    │                  │  │                  │  │ StatusPage       │
    │  InputEditor     │  │  PAGE_UP  → nop  │  │   (stubs)        │
    │  CommandShell    │  │  PAGE_DOWN → nop │  │                  │
    └──────────────────┘  │  ENTER → focus() │  └──────────────────┘
                          └──────────────────┘
```

## Terminal-level intercepts

The following commands are consumed by `AnsiTerminal::step()` and never reach
the active page:

| Command      | Effect                                       |
|--------------|----------------------------------------------|
| `CTRL_L`     | Call `page→focus()` — full screen redraw     |
| `HOME`       | `switch_to(0)` — jump to the home page       |
| `ALT_1…ALT_9`| `switch_to(n-1)` — switch to page n         |
| `ALT_0`      | `switch_to(9)` — switch to page 10 if present|

See [KEYBINDINGS.md](KEYBINDINGS.md) for the full per-page binding table.
