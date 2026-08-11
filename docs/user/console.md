# The console {#user_console}

The console is the unit's operator interface: a full-screen ANSI terminal on the
**console serial port**, showing one page at a time.

## Connecting

 - **Port** : console UART (OPTA: `PG_14` / `PG_9`)
|- **Baudrate** :  `4800` by default
|- **Framing** : `8N1`
 - **Terminal** : `VT100`, `VT102` (default), `MINITEL_1B` or `MINITEL_2`

Since we want to support old terminals such as the Minitel, the default baudrate is
very slow, and the firmware is written so that it is usable on such a low baudrate.

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

The **status bar** is always present on row `0` and contains 3 sections :
- **Tabs** (`X:NAME` : active tab)
- **Clock** (`HH:MM:SS` : wall-clock time)
- **Metrics** (`cpu:XXX%` : duty cycle, `stk:XXX%` : stack usage)

The **alarm bar** lists the active and unacknowledged alar codes. See @ref user_alarms.

## Keyboard shortcuts

These are consumed by the console before the page sees them.

| Key                        | Action                       |
|----------------------------|------------------------------|
| `TAB`, `Ctrl+PageDown`     | next page (wraps)            |
| `Shift+TAB`, `Ctrl+PageUp` | previous page (wraps)        |
| `Alt+1` … `Alt+8`          | jump straight to that page   |
| `HOME`                     | jump to  page 1              |
| `Ctrl+L`                   | clear and repaint everything |
| `Ctrl+C` (in an editor)    | cancel the current prompt    |
| `Ctrl+C` (otherwise)       | exit the simulator           |

**Minitel keypads** map onto the same commands: 
 - ENVOI / RETOUR : act as `ENTER`
 - SOMMAIRE / GUIDE : act as `Alt+1`
 - CORRECTION as `BACKSPACE`
 - ANNULATION as `Ctrl+U`
