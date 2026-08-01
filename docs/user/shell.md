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
| `get [<name>]` | print one variable, or all of them. Marks parameters, overrides, and values nothing has written yet |
| `set <name> <value>` | edit a **parameter** and persist it |
| `override [<name> [off\|<value>]]` | force **any** variable by hand |
| `control [enable\|disable]` | report or change whether the firmware may drive the field |

These are covered in @ref user_variables, @ref user_settings and @ref user_control.
Booleans read and write as `0` / `1`; `on`, `off` and `true` are not accepted as
values.

### Status

| Command | Does |
|---|---|
| `version` | firmware version, build commit, site name, hardware description |
| `uptime` | time since boot |
| `stats` | uptime and tick count, CPU load, jitter, stack usage, and console serial drops - each with its recent and peak figures |
| `net` | network / NTP / MQTT status: link state and mode, IP, netmask, gateway, DNS, NTP offset and server, MQTT state and last publish age. Addresses are read back from the interface, so a DHCP lease shows through |
| `alarms [list \| ack <code\|all>]` | active alarm count; `list` details them; `ack` acknowledges one or all |

### Logging

| Command | Does |
|---|---|
| `logs [on\|off]` | show or hide the live log interleave on this page |
| `loglevel [<level>]` | report or set the minimum level that gets logged at all: `debug`, `info` (default), `notice`, `warning`, `error`, `critical` |

`loglevel` changes what the logger emits to **every** sink, not just this page. See
@ref user_logging.

### Maintenance

| Command | Does |
|---|---|
| `ntpsync` | force an NTP resync now |
| `storage get\|set\|del <key> [<text>]` | raw access to the key-value store, for bringing up or repairing persistence |
| `clear` | wipe the shell scrollback |
| `breakpoint` | trap into an attached debugger |
| `reset` | reboot the controller |
| `exit` | quit (simulator) / reboot (hardware) |

`reset` and `exit` both drop every output to its safe state and clear the screen
before leaving - a reboot on its own gives no such guarantee. See @ref user_control.

@warning `storage set` writes a raw key. It bypasses the validation that `set`
applies, so it can seed a parameter with a value the firmware would otherwise
reject. Use `set` unless you are specifically repairing storage.

## What the site adds

A site may register its own commands; they appear in `help` after the built-ins. The
reference site `gh_mlb41` adds one, `gh`, printing a one-line greenhouse summary.

@note Site commands are matched **before** the built-ins, so a site command named
after a built-in shadows it. Sites should avoid reusing built-in names, `reset` and
`exit` above all.
