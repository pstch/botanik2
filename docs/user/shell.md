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
| `Ctrl+K` | cut from the cursor to end of line |
| `Ctrl+Y` | paste back what `Ctrl+K` cut |
| `Ctrl+C` | abandon the line (it stays echoed above) |

History holds the last 8 commands; consecutive duplicates are not stored, and a line
you were part-way through typing is kept as you scroll away from it.

`TAB` switches pages - there is no completion.

## Commands

### Reading and writing the field

| Command | Does |
|---|---|
| `list [<prefix>]` | browse the variable table, or just the rows whose name starts with `<prefix>`. Each row carries its scope in parentheses |
| `get <name>` | the detail view of one variable, laid out like `stats`: scope, group, access, the raw and override values with their validity, the band, and where it physically comes from |
| `set <name> <value>` | edit a **parameter** and persist it |
| `override list\|clear\|<name> [off\|<value>] [valid\|invalid]` | force **any** variable by hand; `clear` releases the lot |
| `control [enable\|disable]` | report or change whether the firmware may drive the field |

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
| `tasks [<name>]` | the beat, task by task: phase, rate group, period, activations, mean and peak cost. With a name, that one task's schedule and cost in detail. `stats` gives the beat's CPU with no attribution; this is where the number comes from |
| `net` | network / NTP / MQTT status: link state and mode, IP, netmask, gateway, DNS, NTP offset and server, MQTT state and last publish age. Addresses are read back from the interface, so a DHCP lease shows through |
| `alarms [list \| ack <code\|all>]` | active alarm count; `list` details them; `ack` acknowledges one (by code or by variable name) or all |

`tasks` lists phase-major, in registration order within a phase - the order the beat
actually runs them, so the table reads top to bottom as one cycle. The TASKS page
sorts by name instead, which is the right answer for finding a task and the wrong
one for seeing the cycle.

```
botanik> tasks
PHASE   TASK              GROUP    EVERY     RUNS   MEAN us   PEAK us
input   console_rx        fast      10ms    15442         9      1905
...
21 task(s), 0 refused; peaks add to 11501 us of a 10 ms beat
```

Two cautions on the numbers. The peak is an all-time maximum that never decays, so
one outlier pins it for the whole boot - the mean is what to trust for "what does
this cost". And the footer adds those peaks up: it is the beat that never happened,
every task at its own worst all due together, so it exceeding the beat period is
normal and is not by itself an overrun. Real overruns are counted by `stats`.

### Logging

| Command | Does |
|---|---|
| `logs [on\|off]` | show or hide the live log interleave on this page |
| `loglevel [<level>]` | report or set the minimum level that gets logged at all: `debug`, `info` (default), `notice`, `warning`, `error`, `critical` |

`loglevel` changes what the logger emits to **every** sink, not just this page. See
@ref user_logging.

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
| `clear` | wipe the shell scrollback |
| `breakpoint` | trap into an attached debugger |
| `reset` | reboot the controller |
| `exit` | quit (simulator) / reboot (hardware) |

`reset` and `exit` both drop every output to its safe state and clear the screen
before leaving - a reboot on its own gives no such guarantee. See @ref user_control.

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
