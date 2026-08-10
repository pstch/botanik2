# The shell {#user_shell}

The SHELL page is a `botanik>` prompt for inspecting and poking the running
firmware. Type a line, press `ENTER`. An empty line is ignored; an unknown one
answers `Unknown command: …`. `help` lists everything, built-ins first, then any
commands the site adds.

## Editing a line

| Key | Action |
|---|---|
| `ENTER` | run the line |
| `Alt+ENTER`, `Shift+ENTER` | insert a literal newline (multi-line entry) |
| `UP` / `Ctrl+P`, `DOWN` / `Ctrl+N` | previous / next command from history |
| `LEFT` / `Ctrl+B`, `RIGHT` / `Ctrl+F` | move one character |
| `Alt+B`, `Alt+F` | move one word |
| `Ctrl+A`, `END` / `Ctrl+E` | start / end of line |
| `BACKSPACE` / `Ctrl+H`, `DELETE` / `Ctrl+D` | delete backwards / forwards |
| `Ctrl+U` | clear the line |
| `Ctrl+K` | cut from the cursor to end of line (on the line end, joins the next line up) |
| `Ctrl+Space` | drop a mark here, starting a selection (press again to release) |
| `Ctrl+G` | release the mark |
| `Ctrl+W` | cut the selection, or the word before the cursor when there is none |
| `Alt+W` | copy the selection without cutting it |
| `Ctrl+Y` | paste back what was cut or copied |
| `Ctrl+C` | abandon the line (it stays echoed above) |

**Selecting.** `Ctrl+Space` anchors a **mark**; everything between it and the
cursor is the selection, shown in reverse video as you move. `Ctrl+W` cuts it,
`Alt+W` copies it, `Ctrl+G` calls it off. The mark is dropped by any edit - type
a character and the selection is gone - so `Ctrl+W` is a word-kill again unless
you have just marked something. That is deliberate: a selection that survived
edits would drift off the text it was pointing at, and the next `Ctrl+W` would
take the wrong bytes without saying so.

History holds the last 8 commands; consecutive duplicates are not stored, and a line
you were part-way through typing is kept as you scroll away from it.

The cut buffer is **the console's, not the prompt's**: `Ctrl+K` here and `Ctrl+Y`
on the NOTES page are the same clipboard, and `Alt+W` on any of the tables puts
the selected row's name in it. Copy a name off MONITOR, come back here, type
`get ` and `Ctrl+Y`. A cut longer than the buffer (256 bytes) takes what fits and
leaves the rest where it was, so a paste always restores exactly what went.

`TAB` switches pages - there is no completion.

## Commands

### Reading and writing the field

| Command | Does |
|---|---|
| `list [<prefix>]` | browse the variable table, or just the rows whose name starts with `<prefix>`. Each row carries its scope in parentheses |
| `get <name>` | the detail view of one variable, laid out like `stats`: scope, group, access, the raw and override values with their validity, the band, and where it physically comes from |
| `set <name> <value>` | edit a **parameter** and persist it |
| `override list\|clear\|<name> [off\|<value>] [valid\|invalid]` | force **any** variable by hand; `clear` releases the lot |
| `control [enable\|inhibit\|disable]` | report or change whether the firmware may drive the field |

These are covered in @ref user_variables, @ref user_params and @ref user_control.
Booleans read and write as `0` / `1`; `on`, `off` and `true` are not accepted as
values.

Variables are named by domain, so `list` with a prefix is the subsystem view:
`list grid_` shows the grid meter and nothing else.

### Status

| Command | Does |
|---|---|
| `version` | firmware version, build commit, site name, hardware description |
| `features` | which optional subsystems this binary was built with |
| `stats` | uptime and tick count, CPU load, jitter, stack usage, and console serial drops - each with its recent and peak figures. There is no separate `uptime`; `stats` already leads with it |
| `tasks [<name>]` | the beat's totals: how many tasks are registered, the rate groups, total activations and total MISSED ones, what the peaks add up to, and which task costs the most. With a name, that one task's schedule and cost in detail. The per-task table is the TASKS page |
| `net` | network / NTP / MQTT status: link state and mode, IP, netmask, gateway, DNS, NTP offset and server, MQTT state and last publish age. Addresses are read back from the interface, so a DHCP lease shows through |
| `alarms [list \| ack <code\|all>]` | active alarm count; `list` details them; `ack` acknowledges one (by code or by variable name) or all |

`tasks` reads like `stats`: the beat as a whole, not a listing. The per-task table
is the TASKS page, and printing it here as well put twenty-odd rows through a
throttled console to say what a page already draws.

```
botanik> tasks
Registered .................. : 22       (0 refused, 24 slots)
Beat ........................ : 10ms     (fast/main/slow/bkgnd 10/50/100/500 ms)
Activations ................. : 918442   (across 22 task(s))
Missed ...................... : 0        (every task kept its rate)
Peaks add to ................ : 11501us  (every task at its worst, one beat)
Costliest task .............. : 1242us   (console mean, 1.2 % of its own period)
per-task figures: the TASKS page, or `tasks <name>`
```

**Missed** is the headline. A task that comes due late runs **once**; the
activations it slept through are dropped and never caught up, because a burst of
catch-up runs would land exactly when the system is already behind. Nothing else
records that, so a non-zero total is the only sign that some task is not running
at the rate it declared - the detail names the worst offender, and the TASKS page
has the count per task.

**Peaks add to** is the beat that never happened: every task at its own worst, all
due together. It exceeding the beat period is normal and is not by itself an
overrun (real overruns are counted by `stats`). The peak is a per-task all-time
maximum that never decays, so one outlier pins it for the whole boot - which is
why the mean is what `tasks <name>` leads with for "what does this cost".

```
botanik> tasks console
Runs in .............. : present (slow, every 100 ms)
Activations .......... : 15442 run, 0 missed, last 2 ms ago
Cost ................. : last 432 us, mean 611 us, peak 2180 us
Duty ................. : 0.61 % of its own period
```

A task's period is its group's, so `every 100 ms` is what `slow` means on this
build - `tasks` prints the four rates.

### Logging

| Command | Does |
|---|---|
| `logs [on\|off]` | show or hide the live log interleave on this page |
| `loglevel [<level>]` | report or set the minimum level that gets logged at all: `debug`, `info` (default), `notice`, `warning`, `error`, `critical` |

`loglevel` changes what the logger emits to **every** sink, not just this page. See
@ref user_logging. It writes the `sys_log_level` parameter rather than the logger
directly, so the level you set survives a reboot - and `set sys_log_level <n>` or
the PARAMS page reach the same setting.

`features` partitions the optional subsystems into what is present and what is not:

```
botanik> features
on  : console monitor notes network ntp mqtt
off : sun
```

These are fixed at build time - there is no argument form, and nothing here can be
turned on from the console. It is worth asking before you go debugging: an absent
subsystem looks exactly like a broken one from the terminal. No MQTT state in `net`
because MQTT was compiled out reads the same as a broker that will not connect, and
two units on the same firmware **version** can still differ here. See
@ref int_config for what each flag covers.

### Maintenance

| Command | Does |
|---|---|
| `ntpsync` | force an NTP resync now |
| `save` | write the retained values to flash now |
| `clear` | wipe the shell scrollback |
| `breakpoint` | trap into an attached debugger |
| `reset` | reboot the controller |
| `exit` | quit (simulator) / reboot (hardware) |

`reset` and `exit` both drop every output to its safe state and clear the screen
before leaving - a reboot on its own gives no such guarantee. See @ref user_control.

`save` is for the **retained** rows only - the totalizers and hour counters whose
value IS their history. A parameter is written the moment you `set` it, so there
is never anything of its pending; the retained rows are the ones written on a
cadence (`sys_retain_sec`) rather than per change, because writing a litre
counter through would be a flash erase every beat. Use it before pulling a fuse,
after a commissioning run, or as the whole mechanism on a unit running
`sys_retain_sec 0`. A deliberate `reset` / `exit` already writes them, so this is
for the stop the firmware does not get told about.

@note There is no raw `storage` command. Every persisted value belongs to a
parameter or an override and is reached through `set` / `override`, both of which
validate; a back door into the key-value store only ever let someone seed a setting
the firmware would have refused.

## What the site adds

A site may register its own commands; they appear in `help` after the built-ins. The
reference site `gh_mlb41` registers none - everything it exposes is a variable, and
`list` / `get` / `set` already read it.

@note Site commands are matched **before** the built-ins, so a site command named
after a built-in shadows it. Sites should avoid reusing built-in names, `reset` and
`exit` above all.
