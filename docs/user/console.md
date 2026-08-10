# The console {#user_console}

The console is the unit's operator interface: a full-screen ANSI terminal on the
**console serial port**, showing one page at a time.

## Connecting

 - **Port** : console UART (OPTA: `PG_14` / `PG_9`)
 - **Baudrate** : the `serial_console_baud` parameter, `4800` by default
 - **Framing** : `8N1`
 - **Terminal** : the `term_type` parameter - `VT100`, `VT102` (default),
   `MINITEL_1B` or `MINITEL_2`
 - **Size** : the `term_cols` / `term_rows` parameters, `80x24` by default

Since we want to support old terminals such as the Minitel, the default baudrate is
very slow, and the firmware is written so that it is usable on such a low baudrate:
a page repaint arrives over several tenths of a second rather than all at once, and
under load the least important rows are the ones that wait. This is normal.

### Telling the firmware what it is talking to

Which terminal is on the wire and how big it is are properties of the
**installation**, so they are parameters rather than build settings: swapping a
VT102 for a Minitel, or an 80x24 for a 132x32, needs no firmware build. Nothing
is negotiated - there is no resize handshake, so what you declare is what the
pages lay themselves out for.

All three apply together, on a bump of `term_serial`:

```
botanik> set term_type 2       # Minitel 1B
botanik> set term_cols 40
botanik> set term_serial 2     # ...and now apply the lot
```

Staged, because the three describe one physical terminal: applied one edit at a
time the screen would pass through sizes that match nothing on the wire. On the
two Minitel types the firmware emits the PRO2 setup sequences as part of the
switch, exactly as it does at start-up.

@warning The firmware has no way to discover it got this wrong. A size the
terminal does not have is a garbled screen, and it is **persisted**, so it comes
back that way after a reboot. Same hazard as `serial_console_baud`, which can
leave you unable to read the console at all. The monitor port still carries the
log stream if you do lock yourself out.

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
