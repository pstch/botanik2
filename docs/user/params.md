# Parameters {#user_params}

The unit's settings. A parameter is a variable like any other (@ref user_variables) -
it has a name, a type, a unit, a band and a description - with two properties the
others do not have: **you may edit it**, and **it can be made to survive a reboot**.

This page describes the parameters the *library* declares, on every unit whatever
the site. A site declares its own on top, conventionally first in the table; they
behave identically and the same commands reach them.

## An edit is live when you set it, durable when you save it

Two steps, and they are two decisions:

```
botanik> params set ntp_poll_sec 1800
ntp_poll_sec = 1800  (unsaved: `params save ntp_poll_sec`)

botanik> params save ntp_poll_sec
ntp_poll_sec saved
```

`set` applies the value **immediately** and marks the row. `save` writes it to
flash and clears the mark. Nothing writes on your behalf - not a reboot, not
`reset`, not `exit`.

That is what makes a bad setting recoverable. A value that breaks the console it
was typed on was never written, so a power cycle brings the last saved one back.
The cost is that unsaved work is losable, which is why the reply says `(unsaved)`,
why the PARAMS page marks the row with a trailing `*`, and why `params list` ends
with a count of what is still owed:

| Command | Does |
|---|---|
| `params list [<prefix>]` | list parameters, optionally by group, then what is unsaved |
| `params get <name>` | one parameter in detail: band, default, description, notes |
| `params set <name> <value>` | edit it - live at once, not written |
| `params save <name>\|all` | write one row, or every row still owed |
| `params clear <name>\|all` | forget an unsaved edit; the saved value comes back |
| `params reset <name>\|all` | back to the factory default, dropping the saved copy |

A verb acting on the whole set requires the literal `all`: a bare `params save` is
a usage error rather than a commit you did not mean.

Two different ways back, and the difference costs a setting if you pick the wrong
one. `params clear` walks back an edit you have **not** committed: the row returns
to the value a reboot would give it - the saved one, or the factory default when
nothing was ever saved - and what is in storage is left alone. On a row with
nothing outstanding it does nothing and says so.

`params reset` is the heavier one, for a setting whose effect you cannot undo by
typing: it restores the firmware's own default **and** forgets what was saved, so
the next boot does not put it back.

A save can fail, which is a storage fault rather than a bad value: the row keeps
its mark so the unsaved count goes on reporting the debt, and `FLASH_FLT` is
raised (@ref user_alarms). `params set` only works on parameters - pointing it at
a sensor or an output is refused with a hint to use `overrides set` instead, since
the next scan would overwrite it anyway (@ref user_control).

The PARAMS page is the same model with a panel: select a row, read its band and
notes *before* committing, `ENTER` to edit, `^O` to save everything outstanding.
See @ref user_console.

## Applying a group: the `_serial` rows

Most settings take effect the moment they are set. The ones that configure a
**subsystem** do not, and the reason is that they come in sets: bringing the
network up on a half-typed address would take the link down between two commands.

So each such group has a `<group>_serial` row, and the subsystem re-reads the
whole group only when that number moves. Edit the group, then bump the serial:

```
botanik> params set net_dhcp 0
botanik> params set net_ip 192.168.1.40
botanik> params set net_mask 255.255.255.0
botanik> params set net_gw 192.168.1.1
botanik> params set net_serial 2        # <- apply the whole group, at once
botanik> params save all                # <- ...and keep it
```

The value itself means nothing; only *changing* it does. The groups that work
this way are `net_`, `ntp_`, `mqtt_`, `geo_`, `console_` and `mb_`.

## The library's parameters

Registers 900-999 are reserved for these, so a site's own rows never collide.
Anything gated on a feature is absent - not zero - when that feature is compiled
out; `features` from the shell says which are in this binary.

### Console (`console_`)

Everything about the terminal on the console port: how fast it runs, what it is,
and how big. All four apply on `console_serial`, because they describe one
physical terminal - changing the rate on its own, then the size, would ask
whoever is watching to reconnect twice.

| Name | Unit | What |
|---|---|---|
| `console_serial` | | bump to apply staged `console_` edits |
| `console_baud` | Bd | console UART rate. Retunes the wire *and* the render budget; you will have to reconnect at the new rate |
| `console_type` | | which terminal is on the console: `VT100`, `VT102`, `MINITEL_1B`, `MINITEL_2` |
| `console_cols` | col | console width |
| `console_rows` | row | console height |
| `console_home_level` | | lowest level HOME's log tail shows |
| `console_shell_level` | | lowest level the SHELL page interleaves |
| `console_logs_level` | | lowest level the LOGS page keeps |

The three `_level` rows are the exception in this group: they are **not** staged
behind `console_serial`. A level is read when the next entry arrives and
re-initialises nothing, so it lands the moment it is set. See @ref user_logging.

@warning The first four can take the screen with it - a wrong rate, size or make
leaves a console you cannot read. It is deliberately recoverable: the change was
applied and not written, so power-cycling before you `params save all` brings
back the last saved settings.

### Monitor port

| Name | Unit | What |
|---|---|---|
| `monitor_baud` | Bd | monitor UART rate. Log-only port; ignored on the OPTA, whose MONITOR is USB CDC |
| `monitor_level` | | lowest level the port carries. Open by default: this port exists to be the full record |

No `monitor_serial`: the two have nothing to be coherent about, so each applies
where it is read - the rate restarts the port, the level lands on the next entry.

### Network (`net_`, `ntp_`, `mqtt_`)

| Name | Unit | What |
|---|---|---|
| `net_serial` | | bump to apply staged `net_` edits |
| `net_enable` | | bring the network up |
| `net_dhcp` | | enable DHCP IP configuration |
| `net_ip` `net_mask` `net_gw` | | static address, netmask, gateway |
| `net_dns1` `net_dns2` | | static DNS resolvers |
| `ntp_serial` | | bump to apply staged `ntp_` edits |
| `ntp_enable` | | enable NTP clock sync |
| `ntp_poll_sec` | sec | poll interval |
| `ntp_port` | | server port |
| `ntp_srv1` `ntp_srv2` | | NTP servers |
| `mqtt_serial` | | bump to apply staged `mqtt_` edits |
| `mqtt_enable` | | enable MQTT |
| `mqtt_host` `mqtt_port` | | broker host and port |
| `mqtt_ka_sec` | sec | keep-alive interval |
| `mqtt_tlm_sec` | sec | telemetry publish cadence; 0 stops publishing telemetry |
| `mqtt_retain` | | publish telemetry retained |
| `mqtt_level` | | lowest log level published to the broker. Every entry costs a publish, so this is the floor to raise first |

### Modbus TCP slave (`mb_`)

| Name | Unit | What |
|---|---|---|
| `mb_serial` | | bump to apply staged `mb_` edits |
| `mb_enable` | | serve the Modbus TCP slave |
| `mb_port` | | slave port |
| `mb_write_enable` | | let a master write parameters; off makes the slave read-only |

### Modbus RTU master (`rtu_`)

The field bus's timing. These are the plant's to tune, not the build's - a slow
device or a long line is site knowledge.

| Name | Unit | What |
|---|---|---|
| `rtu_timeout_ms` | ms | how long a slave has to answer |
| `rtu_retries` | | immediate retries before a device is backed off |
| `rtu_backoff_ms` | ms | first backoff after a device fails its retries |
| `rtu_backoff_max_ms` | ms | longest a backed-off device waits between probes |
| `rtu_write_min_ms` | ms | shortest interval between writes to one block |
| `rtu_rewrite_ms` | ms | how often an output block is written anyway. **Match the slave's own failsafe timer** - it is watching for exactly this |

### Location (`geo_`)

| Name | Unit | What |
|---|---|---|
| `geo_serial` | | bump to apply staged `geo_` edits |
| `geo_lat_deg` | deg | latitude |
| `geo_lon_deg` | deg | longitude |

### Platform (`sys_`)

Applied live - there is no `sys_serial`, because each of these stands alone.

| Name | Unit | What |
|---|---|---|
| `sys_cpu_load_pct` | % | `CPU_LOAD` alarm threshold |
| `sys_jitter_pct` | % | `CPU_JITT` alarm threshold |
| `sys_stack_pct` | % | `STACK` alarm threshold |
| `sys_log_level` | | lowest level the logger forwards: `debug`, `info`, `notice`, `warning`, `error`, `critical`. The **master** floor, under which no sink sees anything - each sink then has a floor of its own (@ref user_logging) |
| `sys_retain_sec` | sec | how often retained values reach flash. 0 leaves only an explicit save; the trade is flash wear against how much counting a power cut can cost |

`sys_log_level` is an **enumerated** parameter: it is read and written as a word,
and a code with no label is refused outright.

```
botanik> vars get sys_log_level
sys_log_level = debug
...
Range ....................... : debug|info|notice|warning|error|critical

botanik> params set sys_log_level warning
```
