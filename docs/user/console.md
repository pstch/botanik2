# The console {#user_console}

The console is the unit's operator interface: a full-screen ANSI terminal on the
**console serial port**, showing one page at a time.

## Connecting

| | |
|---|---|
| Port | console UART (OPTA: `PG_14` / `PG_9`) |
| Speed | `Config::CONSOLE_BAUD`, **4800** by default |
| Framing | 8N1 |
| Terminal | as built: VT100, VT102 (default), Minitel 1B or Minitel 2 |

The link is deliberately slow, so the firmware budgets every byte it sends: a page
repaint arrives over several tenths of a second rather than all at once, and under
load the least important rows are the ones that wait. This is normal.

The terminal type is a property of the installation, fixed at build time
(`Config::TERMINAL_TYPE`) - it is not negotiated and there is no menu for it. On the
two Minitel settings the firmware emits the PRO2 setup sequences at start-up.

There is a **second** serial port, the monitor port, which carries a plain-text log
stream and no UI. See @ref user_logging.

## The screen

```
┌────────────────────────────────────────────────────────┐
│ 1 2:SHELL 3 4 5 6 7 8  12:04:31      cpu: 12% stk: 31% │  row 0   status bar
├────────────────────────────────────────────────────────┤
│                                                        │
│                    page content                        │  rows 1..N-2
│                                                        │
├────────────────────────────────────────────────────────┤
│ ALARMS : TOO_HOT WTR_LK                                │  row N-1  alarm bar
└────────────────────────────────────────────────────────┘
```

**Status bar** (row 0, always present, reverse video):

- **Tabs**, left. One entry per page. Inactive pages show only their number; the
  active one expands to `N:NAME` and is drawn in normal video.
- **Clock**, centre. `HH:MM:SS`, wall time. Dropped if the screen is too narrow.
- **Metrics**, right. `cpu:nnn%` is the loop's CPU duty cycle, `stk:nnn%` its peak
  stack usage. Both are live health indicators; see @ref user_control.

**Alarm bar** (bottom row) appears only while at least one alarm is **active and
unacknowledged**, and lists their codes. Acknowledging drops a code off the bar
without clearing the alarm. When no alarm qualifies the row reverts to the page.
See @ref user_alarms.

## Keys that work everywhere

These are consumed by the console before the page sees them.

| Key | Action |
|---|---|
| `TAB`, `Ctrl+PageDown` | next page (wraps) |
| `Shift+TAB`, `Ctrl+PageUp` | previous page (wraps) |
| `Alt+1` … `Alt+8` | jump straight to that page |
| `HOME` | jump to page 1 |
| `Ctrl+L` | clear and repaint everything |
| `Ctrl+C` | offered to the page first (it usually means "cancel"); if the page declines, quits the simulator and does nothing on hardware |

A stray `Ctrl+C` can never reboot a controller - that is deliberate.

@note Because the console claims `HOME` for page 1, it never reaches a page. Where a
page would use it, `ENTER` (jump to top of a table) or `Ctrl+A` (start of line, in an
editor) do the same job.

@note A lone `ESC` is only recognised once the firmware is sure no escape sequence
follows, so it takes about 100 ms to register. `Ctrl+C` cancels instantly and is the
better habit.

**Minitel keypads** map onto the same commands: ENVOI / RETOUR act as `ENTER`,
SOMMAIRE / GUIDE as `Alt+1`, CORRECTION as `BACKSPACE`, ANNULATION as `Ctrl+U`.

Held `BACKSPACE` and `Ctrl+D` are coalesced: the firmware handles a burst of repeats
in one pass rather than redrawing per keystroke.
