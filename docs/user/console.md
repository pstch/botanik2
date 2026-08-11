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

### Confirming, because the firmware cannot check

Nothing here can be verified from this end: there is no resize handshake and no
rate negotiation, so a wrong answer is only visible to the person in front of the
terminal. That is what the confirmation is for. A commit **applies** the new
settings and starts a 60-second probation, and only `console confirm` makes them
durable:

```
botanik> set serial_console_baud 9600
botanik> set term_serial 3
BOOT: console now 9600 baud, 80x24, terminal type 1
      type `console confirm` within 60 s or it reverts

... reconnect the terminal at 9600 ...

botanik> console confirm
console: confirmed; saving these settings
```

Being able to read that prompt and type the answer is itself the proof the link
works in both directions. Anything else puts the previous settings back:

| | |
|---|---|
| `console` | how long is left, when a probation is running |
| `console confirm` | keep these settings, and write them to flash |
| `console revert` | put the previous ones back now, without waiting |
| *(silence)* | after 60 s, the same revert |
| *(power cut)* | the settings never reached flash, so the unit boots on the last confirmed ones |

The rate is the reason for the 60 seconds: changing it means physically
reconnecting a terminal at the new speed. `console confirm` covers the size and
the make in the same breath, because a terminal swap is usually all three.

@note Getting the **size** wrong is survivable without any of this. The screen
mangles, but input is untouched, so `set term_cols 80` and a `term_serial` bump
typed blind will still land. It is the **rate** that leaves nothing to type into,
and that is the failure probation exists for.

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
