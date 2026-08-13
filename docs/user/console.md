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
botanik> params set term_type 2       # Minitel 1B
botanik> params set term_cols 40
botanik> params set term_serial 2     # ...and now apply the lot
```

Staged, because the three describe one physical terminal: applied one edit at a
time the screen would pass through sizes that match nothing on the wire. On the
two Minitel types the firmware emits the PRO2 setup sequences as part of the
switch, exactly as it does at start-up.

### Getting it wrong

Nothing here can be verified from this end: there is no resize handshake and no
rate negotiation, so a wrong answer is only visible to the person in front of the
terminal. What makes that survivable is the rule the whole variable table
follows - **an edit is live when you set it and durable when you save it** - so a
console nobody can read was never written to flash, and a power cycle brings the
last saved one back.

```
botanik> params set serial_console_baud 9600
botanik> params set term_serial 3
BOOT: console now 9600 baud, 80x24, terminal type 1
      `params save` to keep it; a power cycle undoes it

... reconnect the terminal at 9600. If that works:

botanik> params save
```

So there is nothing special to remember for the console. If the new settings are
readable, `params save`. If they are not, pull the power: the unit comes back on
whatever was last saved.

@note Getting the **size** wrong is milder still. The screen mangles, but input
is untouched, so `params set term_cols 80` and a `term_serial` bump typed blind
will land even on a screen you cannot read. It is the **rate** that leaves
nothing to type into, and the power cycle is what answers that one.

There is a **second** serial port, the monitor port, which carries a plain-text log
stream and no UI. See @ref user_logging.

@note The screens in this page are captures from the reference site `gh_mlb41`. The
layout, the keys and the commands are the library's and are the same on every unit;
the variable names, parameters and readings in them are that site's.

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

The **alarm bar** lists the active and unacknowledged alarm codes. See
@ref user_alarms.

## Keyboard shortcuts

These are consumed by the console before the page sees them.

| Key                        | Action                       |
|----------------------------|------------------------------|
| `TAB`, `Ctrl+PageDown`     | next page (wraps)            |
| `Shift+TAB`, `Ctrl+PageUp` | previous page (wraps)        |
| `Alt+1` … `Alt+8`          | jump straight to that page   |
| `HOME`                     | jump to  page 1              |
| `Alt+W`  (in a table)      | copy the cell's value        |
| `Ctrl+L`                   | clear and repaint everything |
| `Ctrl+C` (in an editor)    | cancel the current prompt    |
| `Ctrl+C` (otherwise)       | exit the simulator           |

**Minitel keypads** map onto the same commands: 
 - ENVOI / RETOUR : act as `ENTER`
 - SOMMAIRE / GUIDE : act as `Alt+1`
 - CORRECTION as `BACKSPACE`
 - ANNULATION as `Ctrl+U`

### Kill ring

Cut and paste are the console's, not one page's: there is a single clipboard - the
**kill ring** - and every place that holds text shares it. That is the point. On a
terminal with no mouse, the way to get text from one place to another is to cut it
here and yank it there.

| Key | Action |
|---|---|
| `Ctrl+K` | cut from the cursor to the end of the line |
| `Ctrl+Space` | drop a **mark** at the cursor |
| `Ctrl+W` | cut the marked region, or the previous word when there is no mark |
| `Alt+W` | copy without cutting - the marked region, or the selected cell on a table page |
| `Ctrl+Y` | yank: paste back what was last cut or copied |
| `Ctrl+G` | drop the mark, cutting nothing |

Three things follow from one shared ring:

- a line cut at the shell prompt yanks into the NOTES page, which is how a command
  and its answer become a note;
- `Alt+W` on a table row (MONITOR, PARAMS, TASKS, ALARMS) puts the selected row's
  **name** in the ring, which is the short way from a row you are looking at to a
  `vars get` or `params set` about it;
- the mark is **transient** - any edit drops it - so there is no stale selection to
  drift off the text it pointed at.

A cut takes only what the ring can hold and leaves the rest in the buffer, so a yank
always restores exactly what went.

## The pages

Eight pages ship in the default tab order. `TAB` and `Shift+TAB` walk the strip and
wrap; `Alt+<n>` jumps straight to one; `HOME` goes back to page 1. The status bar's
tab strip is the map: the active page shows as `N:NAME`, the rest as bare digits.

| # | Page | For |
|---|---|---|
| 1 | HOME | at-a-glance readings and a live log tail |
| 2 | SHELL | the command prompt |
| 3 | LOGS | the full scrolling log |
| 4 | ALARMS | the alarm table, and acknowledging alarms |
| 5 | PARAMS | reading and editing parameters |
| 6 | MONITOR | every published variable |
| 7 | TASKS | what the loop is spending its time on |
| 8 | NOTES | free-text notes kept on the unit |

The four table pages (ALARMS, PARAMS, MONITOR, TASKS) are driven the same way:
`UP`/`DOWN` move the selection, `LEFT`/`RIGHT` change the view mode shown in the
footer badge, `SPACE` steps to the next mode, and the panel below the table details
the selected row. Whatever they are looking at, `Alt+W` copies the selected row's
name.

### Home page

```
 1:HOME 2 3 4 5 6 7 8               00:00:07                  cpu:100% stk:  2%
    _           _              _ _
   | |__   ___ | |_ __ _ _ __ (_) | __
   | '_ \ / _ \| __/ _` | '_ \| | |/ /        Site name ...... : gh_mlb41
   | |_) | (_) | || (_| | | | | |   <         Version ........ : v0.13.12-dev
   |_.__/ \___/ \__\__,_|_| |_|_|_|\_\        Commit ......... : 142b159-dirty

  Uptime     000d 00:00:06  L1 V       231.2 V        ATS V      230.0 V
                            L2 V       229.2 V        ATS A      4.8 A
  Line V     230.0 V        L3 V       230.3 V        ATS Hz     50.00 Hz
  Line-line  398.4 V        L1 A       1.3 A          Supply Hz  50.00 Hz
  Line Hz    50.00 Hz       L2 A       1.3 A          Lockout    0
  Current    4.0 A          L3 A       1.3 A          Screen     0
  Power      2429 W         Neutral A  0.0 A          Skirt      0
  Pwr factor 0.880          THD L-N    0.0 %

    LOGS

26/01/01 00:00:00.010 [+] NETWORK: network up: 127.0.0.1
26/01/01 00:00:00.030 [+] NETWORK: MQTT connected (192.168.1.43:58443)
26/01/01 00:00:00.050 [+] TIME: NTP synced (127.2.223.135:123)
 ALARMS :
```

The landing screen, and the one to leave a unit sitting on. Three bands: the
identity block (site, firmware version, build commit), up to 22 **home fields** in
three columns - label and live value, chosen by the site - and a live log tail at
the bottom.

Nothing is editable here. It answers "what is this unit and what is it doing right
now" at a glance, from across the room.

### Shell page

```
 1 2:SHELL 3 4 5 6 7 8              00:00:11                  cpu:100% stk:  2%
botanik> stats
Uptime ...................... : 11s
CPU ......................... : 12%      (peak 41%)
Jitter ...................... : 0 ms     (peak 2 ms)
Stack ....................... : 2%       (high 1.2 kB)

botanik> _
 ALARMS :
```
The shell provides a prompt for inspecting, monitoring and interacting with the
running firmware. The shell page supports scrolling, multi-line editing and 
interleaves log lines with the shell's output.

#### Keyboard shortcuts

Standard emacs-like shortcuts.

| Key                                         | Action                                                               |
|---------------------------------------------|----------------------------------------------------------------------|
| `ENTER`                                     | execute the editor's input command                                   |
| `Alt+ENTER`, `Shift+ENTER`                  | insert a literal newline                                             |
| `UP` / `Ctrl+P`,                            | previous command from history                                        |
| `DOWN` / `Ctrl+N`                           | next command from history                                            |
| `LEFT` / `Ctrl+B`                           | move to the previous character                                       |
| `RIGHT` / `Ctrl+F`                          | move to the next character                                           |
| `Alt+B`                                     | move to the previous word                                            |
| `Alt+F`                                     | move to the next word                                                |
| `Ctrl+A`                                    | move to the beginning of the line                                    |
| `END` / `Ctrl+E`                            | move to the end of the line                                          |
| `BACKSPACE` / `Ctrl+H`                      | delete the previous character                                        |
| `DELETE` / `Ctrl+D`                         | delete the next character                                            |
| `Ctrl+U`                                    | clear the editor's input                                             |
| `Ctrl+K`                                    | cut to the end of the line                                           |
| `Ctrl+Space`                                | begin or drop a selection                                            |
| `Ctrl+G`                                    | release the mark                                                     |
| `Ctrl+W`                                    | cut the selection or previous word                                   |
| `Alt+W`                                     | copy the selection without cutting it                                |
| `Ctrl+Y`                                    | paste back what was cut or copied                                    |
| `Ctrl+C`                                    | print & forget the editor's input                                    |
 
#### Shell commands

Commands are generally **noun then verb**: 
 - the noun is what you are working on
 - the verb is what you are doing to it. 

`help` lists all commands. The site-specific code may register additional commands
or override built-in commands with their own code.

##### System control

| Command                                        | Action                                |
|------------------------------------------------|---------------------------------------|
| `logs [on\|off]`                               | show/hide logs on the shell page      |
| `control [enable\|inhibit\|disable]`           | disable or inhibit the control logic  |
| `ntpsync`                                      | request an immediate NTP sync         |
| `breakpoint`                                   | trap into an attached debugger        |
| `reboot`                                       | drop control, then let the watchdog reset the unit |
| `reset`                                        | reset the unit now                    |
| `exit`                                         | quit (simulator) / reboot (hardware)  |

  - `reset` cuts the loop off where it stands and may leave outputs in a non-safe state
  - `reboot` leaves the loop running, but disables control and stops kicking the watchdog

##### System status

| Command                                        | Action                                |
|------------------------------------------------|---------------------------------------|
| `stats`                                        | show system statistics                |
| `net`                                          | show network, MQTT & NTP state        |
| `modbus`                                       | show ModBus status                    |
| `tasks`                                        | the beat's totals                     |
| `tasks list [<prefix>]`                        | list tasks                            |
| `tasks get <name>`                             | show task state                       |
| `version`                                      | show the firmware version             |
| `features`                                     | show enabled features                 |

##### Parameters, overrides, alarms and other variables

| Command                                        | Action                                |
|------------------------------------------------|---------------------------------------|
| `params list [<prefix>]`                       | list parameters                       |
| `params get <name>`                            | get a parameter                       |
| `params set <name> <value>`                    | set a parameter (live, unsaved)       |
| `params clear <name>\|all`                     | reset parameters to their default     |
| `params save <name>\|all`                      | persist a parameter, or all of them   |
| `overrides list`                               | list overrides                        |
| `overrides get <name>`                         | get an override                       |
| `overrides set <name> [<value>] [invalid]`     | set an override                       |
| `overrides clear <name>\|all`                  | release one override, or all of them  |
| `overrides save <name>\|all`                   | persist an override, or all of them   |
| `alarms list [<prefix>]`                       | list active alarms                    |
| `alarms get <code\|name>`                      | get an alarm                          |
| `alarms ack <code\|name\|all>`                 | acknowledge an alarm                  |
| `vars list [<prefix>]`                         | list variables                        |
| `vars get <name>`                              | read a variable                       |
| `programs list [<prefix>]`                     | list programs                         |
| `programs enable <name>\|all`                  | enable a program, or all of them      |
| `programs disable <name>\|all`                 | disable a program, or all of them     |
| `programs save <name>\|all`                    | persist to flash disabled programs    |

Every listing takes a name prefix, and variables are conventionally named by domain (ex.
`grid_`, `irr_`, `cli_`), so using name prefixes allow to view items "per subsystem".

##### Console

| Command                                        | Action                                |
|------------------------------------------------|---------------------------------------|
| `help`                                         | list every command, with its usage    |
| `clear`                                        | clear the shell page's output         |

##### Site commands

A site may register commands of its own - a wash cycle, a valve exercise, a report
that only makes sense for that installation. They are tried **before** the built-ins,
so a site can also replace one, and `help` lists them after them. What your unit has
is what `help` prints; this manual can only describe the library's own.

### Logs page

```
 1 2 3:LOGS 4 5 6 7 8               00:00:15                  cpu:100% stk:  2%
26/01/01 00:00:00.000 [~] BOOT: Params initialized
26/01/01 00:00:00.000 [~] BOOT: NetworkHandler initialized
26/01/01 00:00:00.000 [~] BOOT: Scan initialized
26/01/01 00:00:00.000 [~] BOOT: Console initialized
26/01/01 00:00:00.000 [~] BOOT: boot complete
                        > HW watchdog 10000 ms
26/01/01 00:00:00.000 [~] BOOT: console 4800 baud
26/01/01 00:00:00.000 [~] BOOT: monitor 115200 baud
26/01/01 00:00:00.010 [-] NETWORK: network bring-up (static)
26/01/01 00:00:00.010 [+] NETWORK: network up: 127.0.0.1
26/01/01 00:00:00.030 [+] NETWORK: MQTT connected (192.168.1.43:58443)
26/01/01 00:00:00.050 [+] TIME: NTP synced (127.2.223.135:123)
 ALARMS :
```

The whole log, newest at the bottom, scrolling as entries arrive. The band is a
circular buffer, so it holds a bounded history and the oldest lines fall off the
top. A long entry soft-wraps onto a continuation row marked `>`.

What a line means - the timestamp, the level badge, the topic - is in
@ref user_logging.

### Alarms page

```
 1 2 3 4:ALARMS 5 6 7 8             00:00:19                  cpu:100% stk:  2%

  | CODE     | VARIABLE                          | ON | OVR | LAST EVENT     |
  +----------+-----------------------------------+----+-----+----------------+
  | TOO_HOT  | cli_zone1_over_temp               | ON |     | 12:03:41       |
  | WTR_LK   | irr_leak_detected                 |    |     | 11:58:02       |
  +----------+-----------------------------------+----+-----+----------------+

 Description  : zone 1 above its high-temperature limit
 Remediation  : check vent actuators and the zone 1 sensor
 Acknowledged : -

  unmasked <  [recent]     > all    1-2 / 2       1 active   a:ack

 ALARMS :
```

The alarm table, and the only place an alarm can be acknowledged: select a row and
press `a`. The panel below shows the selected alarm's description, its remediation
note - written by the site, in terms of the real installation - and when it was
acknowledged.

`LEFT` / `RIGHT` / `SPACE` change the filter, shown in the footer badge:

| Filter | Shows |
|---|---|
| `unmasked` | exactly what the alarm bar shows: active and not acknowledged |
| `recent` | anything that happened lately, cleared included |
| `active` | everything currently active, acknowledged or not |
| `all` | every alarm condition the unit has, whether it has ever fired or not |

`all` is the one to reach for when you want to know what this unit *can* tell you.
The lifecycle behind the columns is in @ref user_alarms.

### Parameters page

```
 1 2 3 4 5:PARAMS 6 7 8             00:00:23                  cpu:100% stk:  2%

  | PARAMETER                | TYPE | VALUE                 | UNIT  | REGISTER |
  +--------------------------+------+-----------------------+-------+----------+
  | serial_console_baud      | u32  | 4800                  | Bd    | 900      |
  | serial_monitor_baud      | u32  | 115200                | Bd    | 901      |
  | net_serial               | u32  | 1                     |       | 910      |
  | net_dhcp                 | bool | 0                     |       | 911      |
  | net_ip                   | str  | 192.168.1.40*         |       | 912      |
  | net_mask                 | str  | 255.255.255.0         |       | 913      |
  ----------------------------------------------------------------------------

 Minimum      : 1200        Maximum      : 921600      Default  : 4800
 Description  : console UART rate
 Notes        : retunes the wire AND the render budget; reconnect at the new rat

                [all]    (serial)   1-11 / 170   1 unsaved (*) - ^O to save

 ALARMS :
```

Reading and editing the unit's settings. Select a row and the panel gives its
minimum, maximum, default, description and notes - what you need *before* committing
to a value. What each library-provided parameter means, and the two-step edit behind
the `*` marks, is @ref user_params.

`ENTER` opens an inline editor on the bottom row; type, `ENTER` again to apply, `ESC`
to abandon. An applied value is live immediately and marked with a trailing `*`,
with the footer counting how many rows are in that state; **`^O` writes them all to
flash**. A value the parameter's band refuses is rejected in place, marked
`[invalid]`, and the editor stays open.

`LEFT` / `RIGHT` / `SPACE` filter by group, the badge showing which. The `REGISTER`
column is the Modbus TCP register an external master would use for that row, blank
where the row is not published.

### Monitor page

```
 1 2 3 4 5 6:MONITOR 7 8            00:00:28                  cpu:100% stk:  2%

  | SCOPE    | VARIABLE                 | VALUE         | UNIT   | OVERRIDE  |
  +----------+--------------------------+---------------+--------+-----------+
  | SV       | sys_fieldbus_dead        | 0             |        | -         |
  | MI       | grid_L1_V                | 228.6         | V      | -         |
  | MI       | grid_L2_V                | 226.6         | V      | -         |
  | MI       | grid_L3_V                | 227.7         | V      | -         |
  | MI       | grid_L1_A                | 1.3           | A      | -         |
  v----------+--------------------------+---------------+--------+-----------v

 Description  : field devices not answering
 Notes        : 0 when every planned device is talking
 Mapping      : computed
 Quality      : valid, by no program

                [all]      (sys)    1-10 / 107

 ALARMS :
```

Every published variable, read straight off the table: inputs, outputs, computed
values and status values, grouped by name prefix. Read-only - this is the page for
*looking*, and `overrides set` is how you write.

The panel gives the selected row's description, notes, where the value physically
comes from (`Mapping`), and its quality - the last being the one that answers "is
this number worth believing". The `OVERRIDE` column flags a row someone has forced by
hand. `LEFT` / `RIGHT` / `SPACE` step through the groups.

The `v` in the table's bottom rule means there is more below; `^` means more above.

### Tasks page

```
 1 2 3 4 5 6 7:TASKS 8              00:00:32                  cpu:100% stk:  2%

  | TASK                  | GROUP | EVERY ms | PHASE   | MEAN us  | MISSED   |
  +-----------------------+-------+----------+---------+----------+----------+
  | alarms                | slow  | 100      | service | 0        | 0        |
  | console               | slow  | 100      | present | 0        | 0        |
  | console_rx            | fast  | 10       | input   | 0        | 0        |
  | expire_stale          | bkgnd | 500      | input   | 0        | 0        |
  | modbus_rtu_rx         | fast  | 10       | input   | 0        | 0        |
  ----------------------------------------------------------------------------

 Last us      : 0                     Peak us      : 0
 Runs         : 323                   Missed       : 0
 Of period    : 0.0 %

 ALARMS :
```

What the loop is spending its time on, one row per registered task. `stats` gives
the beat's CPU with no attribution; this is the attribution.

**MISSED** is the column to run an eye down. A task that missed activations did not
run at the rate it declared, and a missed activation is never caught up - so this is
the only record that it happened. The panel adds the selected task's last and peak
cost, its run count, and what share of its own period it uses.

The shell answers the same questions without leaving the prompt: `tasks` for the
beat's totals, `tasks list` for this table, `tasks get <name>` for one task's panel.
What each library-provided task does, and what its phase and rate mean, is
@ref user_tasks.

### Notes page

```
 1 2 3 4 5 6 7 8:NOTES              00:00:36                  cpu:100% stk:  2%
> Relay K3 replaced 2026-04-11, spare in the cabinet.
> Zone 2 sensor reads 0.4 C high - trim pending.
> _




 0/2047 bytes  saved  ^O save
 ALARMS :
```

A free-text note kept on the unit itself, for the things that belong to *this*
installation and nowhere else: what was replaced and when, which sensor lies, what
the next person needs to know. It is a full-screen editor with the same keys as the
shell prompt, and the same kill ring - so a line cut at the prompt yanks in here.

`^O` writes the note to storage; the footer shows the byte count and whether what is
on screen has been saved. Leaving the page flushes it too, so a note cannot be lost
by wandering off. The text survives a reboot, and it is the one page whose content
the firmware never writes to itself.
