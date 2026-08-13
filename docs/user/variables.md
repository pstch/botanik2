# Variables {#user_variables}

Everything the firmware exposes is a **variable**: a meter reading, a relay coil, a
value the control programs compute, a setting. They all have a name, a type, a unit,
a description and a plausibility band, and they are all read the same way.

What differs is what a variable is *for*:

| Role | Behaviour |
|---|---|
| input | comes from the field; goes invalid if the driver stops refreshing it, or if the reading leaves its declared band |
| output | drives the field; drops to its safe state when control is withdrawn |
| computed | worked out by the control programs; never expires |
| parameter | editable and saved (see [Parameters](#user_variables_params) below) |

An input is invalidated two ways, and the second one matters more than it sounds: a
reading can be perfectly fresh and still be nonsense. A dead 4-20 mA loop reads full
scale forever, which no timeout catches - the plausibility band does.

## Reading one

Two commands, two depths. `vars list` browses - one row per variable, optionally
filtered by name prefix; the MONITOR page shows the same set grouped, on screen
(see @ref user_console).

```
botanik> vars list grid_L1_
  grid_L1_V            237.4        V    (MI)
  grid_L1_A            7.6          A    (MI)
  2 variable(s)
```

`vars get` is the detail view of ONE row:

```
botanik> vars get temp_zone1
temp_zone1 = 35.0 C
Scope ....................... : AI       (input, calibrated)
Group ....................... : temp
Access ...................... : read-only
Raw value ................... : 21.3 C   (valid)
Override value .............. : 35.0 C   (valid, unsaved)
Plausible ................... : -20.0 .. 60.0 C
Source ...................... : Modbus dev 11 fc 3 reg 4
Description ................. : zone 1 air temperature
```

Laid out like `stats` and `net`, so the three commands that answer "what is the
state of X" read the same way.

**Raw value** and **Override value** are the pair `vars list` cannot give you: what
the row reads underneath, and what it is being held at. "Has the sensor come back
yet?" is the question someone deciding whether to release an override actually
has. Each says whether it is worth believing - a forced value can be good or made
to read as a dead sensor, and so can the reading under it. `Override value : -`
means nothing is forced.

Three annotations on a `vars list` row matter:

- `(P)`, `(MI)`, `(RO)`, ... - what kind of row it is. Every row carries it:
  which one you are looking at is what decides whether `params set` or
  `overrides set` applies.
- `OVERRIDDEN` - someone has forced it by hand; the number is not the live one.
- `(default: nothing has written it)` - no driver has ever published this variable,
  or it timed out. The number shown is the **declared default**, not a measurement.

That last one is the important one to recognise. A dead sensor does not read as
zero-because-broken; it reads as whatever the site declared as its fallback, and the
firmware tells you so.

### Why a reading is invalid

`vars get` and the MONITOR panel do not just say a value is bad, they say what made
it bad - because the four causes want four different next moves:

| Reads | What happened | Where to look |
|---|---|---|
| `valid` | a real value from whoever owns the row | - |
| `invalid: nothing has written it` | no driver has ever published it | wiring, or a device that never answered |
| `invalid: past its timeout` | it was live and stopped | the device went away mid-run |
| `invalid: outside its band` | it is answering, with nonsense | the sensor - a dead 4-20 mA loop reads full scale, and no timeout catches that |
| `invalid: dropped to its safe state` | an **output**, not a fault | control authority is withdrawn (see @ref user_control) |

The last one is worth recognising: a relay reading invalid usually means the
firmware is deliberately not driving it, not that anything is broken.

## Overrides {#user_variables_overrides}

An override forces a variable to a value you choose. It is a maintenance tool: fake
a sensor to exercise the logic watching it, freeze a computed value, or drive an
actuator by hand.

```
botanik> overrides set temp_zone1 35.0        # force a value
botanik> overrides set temp_zone1             # hold it at whatever it reads now
botanik> overrides set temp_zone1 35.0 invalid  # force the value AND make it read as dead
botanik> overrides clear temp_zone1           # release it
botanik> overrides list                       # everything currently forced
botanik> overrides clear                      # release everything
botanik> overrides save                       # make what is forced survive a reboot
```

A forced value reads as **good** unless you say `invalid`, the bare form included: a
variable nothing had ever written still comes out valid once you hold it, because
holding a value is asserting it.

Five things are worth understanding:

**An override is seen by everything.** It is applied at the single point every
reader goes through, so the control programs, the console, the alarm model and MQTT
telemetry all see the forced value. Forcing a temperature really will make the
control programs act on it. That is the point, and it is also the risk.

**It shadows, it does not replace.** The driver keeps publishing underneath. Clearing
the override snaps straight back to the live reading, with no gap and nothing to
restore.

**It dies with the session unless you save it.** A new override is live immediately
and marked `(unsaved)`: a reboot releases it. That is the right default, because
forcing a signal to chase a fault is the common case and it should not outlive the
person who did it. An override left in place *on purpose* - a failed sensor bypassed
until the spare arrives - is exactly the one to commit:

```
botanik> overrides save temp_zone1      # this one
botanik> overrides save                 # everything still unsaved
```

`overrides list` shows what is forced and how much of it is unsaved. Releasing one
(`overrides clear <name>|all`) forgets the stored copy in the same breath, so nothing comes
back at the next boot.

@warning **A saved override on an output is driving the actuator from the moment the
unit boots.** Every restored override is announced as a **critical** on the `BOOT`
topic naming the row, and an output's says so in as many words - that announcement is
the only thing standing between a forgotten override and a surprise. `overrides list`
before you walk away.

**It can force the reading's quality too.** A trailing `valid` / `invalid` says what
the forced value reads as, so "force this to 20 C" and "make this read as a dead
sensor" are both expressible. MONITOR reports the second as
`OVERRIDDEN (forced invalid)` and `overrides list` flags it as
`(reading as invalid)`. The forced value is still what a reader gets, so this is not
the same as simulating the timeout fallback.

Unlike `params set`, an override is checked only against the variable's *type*, not
its declared plausibility band - so you can push a reading past what the site
considers possible, which is usually the whole reason for forcing it.

@note **Control authority and overrides are complementary, not ranked.**
`control inhibit` stops the control *programs*; an override still reaches an output,
and with the scan stopped it is the only thing that does - which is how you drive a
valve by hand. The exception is the firmware withdrawing control *itself* (a health
fault, a pending reset): that one does release an override on an output, because
nobody is necessarily watching. See @ref user_control.

## Parameters {#user_variables_params}

A parameter is just a variable with the parameter role: editable, range-checked, and
saved. Everything else about it - name, type, unit, description - works exactly as
above. The PARAMS page and the `params` command are the two ways to reach one, and
both call them parameters; nothing in the firmware calls them "settings".

### Editing one

Editing and saving are **two steps**, deliberately. `params set` applies the value
immediately - the plant is running on it from that moment - but only `params save <name>|all`
makes it survive a reboot. So a setting can be tried on the real process and walked
back by a power cycle, and a tuning session can be committed once at the end rather
than a row at a time.

```
botanik> params set cli_sp_day 22.5
cli_sp_day = 22.5  (unsaved: `params save cli_sp_day`)
botanik> params save cli_sp_day
cli_sp_day saved
```

| Result | Meaning |
|---|---|
| `<name> = <value>  (unsaved: ...)` | applied and live, not yet durable |
| `params: invalid value for <name>` | rejected: wrong format or out of range. Nothing changed |
| `<name> saved` | written to storage; it survives a reboot |
| `<name> NOT saved: storage refused it` | **a storage fault**, not a bad value. The value stays live and stays owed, so the next `params save all` tries again |

That last case also raises the `FLASH_FLT` alarm. See @ref user_alarms.

The same two steps on the PARAMS page: select, `ENTER`, type, `ENTER` to apply -
the value picks up a `*` and the footer counts what is unsaved - then `^O` to write
every unsaved row at once. The panel shows the minimum, maximum and description
before you commit.

`params save all` commits everything outstanding at once - the whole set is
only ever spelled `all`, so a bare `params save` is a usage error rather than a
commit you did not mean:

```
botanik> params save all
params: 3 saved
```

`params set` only works on parameters. Pointing it at a sensor or an output is
refused with a hint to use `overrides set` instead: writing those would simply be
overwritten on the next scan.

Storage is one key per parameter, named after it, so adding, removing or reordering
parameters never invalidates the others.

### Applying a group of parameters

Network, NTP, MQTT and geo parameters are grouped, and each group has a **commit
serial** - a parameter named `<group>_serial`, so it files under its own group like
every other row in it. The component that consumes a group re-reads all of it, and
restarts, when that serial changes.

@warning Editing a parameter does **not** apply it to the component that reads it.
The value is live in the table, but the component keeps running on what it read
last. You must bump the group's serial to make it take effect.

So a network change is three steps: edit, commit the group, save.

```
botanik> params set net_dhcp 0
botanik> params set net_ip 192.168.1.40
botanik> params set net_mask 255.255.255.0
botanik> params set net_gw 192.168.1.1
botanik> params set net_dns1 192.168.1.1
botanik> params set net_serial 2        # <- apply the whole group
botanik> params save                    # <- and keep it across a reboot
```

Any new serial will do, as long as it differs from the current one; incrementing is
the convention. Staging the whole group before committing is the point of the design:
a static IP block is only coherent once every field is set, and this way the interface
restarts once rather than five times on its way through a half-configured state.

Leaving the group unsaved is a real (and useful) state: the new address is in force
now, and a power cycle brings back the one that was known to work.

### The groups the library itself reads

@note What follows is **not the parameter list of your unit.** These four groups are
the ones the library's own components consume, so they exist on every build with the
matching subsystem, and they are the only ones this manual can name. A greenhouse
declares its own parameters in its site table - setpoints, dwell times, calibration
trims - and those are documented with the site, not here.

`params list` with a prefix and the PARAMS page are the authoritative list of what
your unit actually has: `params list irr_` shows the irrigation parameters whether or
not any page of this manual has heard of them.

The names below are fixed, but every **default** is the site's - the values shown are
the reference site `gh_mlb41`'s.

#### Network parameters

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `net_serial` | u32 | 1 | commit serial - bump to re-apply the group |
| `net_dhcp` | bool | 0 | 1 = DHCP, 0 = static |
| `net_ip` | str | `192.168.1.42` | static IPv4 address |
| `net_mask` | str | `255.255.255.0` | static subnet mask |
| `net_gw` | str | `192.168.1.1` | default gateway |
| `net_dns1` | str | `8.8.8.8` | primary DNS server |
| `net_dns2` | str | `4.4.4.4` | secondary DNS server (may be empty) |

The static fields are ignored while `net_dhcp` is 1. `net` reports what the
interface actually ended up with, which is how you confirm a lease.

#### Time parameters (NTP)

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ntp_serial` | u32 | 1 | commit serial |
| `ntp_enable` | bool | 1 | enable time sync |
| `ntp_poll_sec` | u32 | 3600 s | poll interval |
| `ntp_srv1` | str | `0.fr.pool.ntp.org` | primary server (host or IP) |
| `ntp_srv2` | str | `1.fr.pool.ntp.org` | secondary server (empty = no fallback) |

`ntpsync` forces a resync without waiting for the next poll. The status-bar clock
and every log timestamp follow this.

#### Telemetry parameters (MQTT)

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `mqtt_serial` | u32 | 1 | commit serial |
| `mqtt_enable` | bool | 1 | enable publishing |
| `mqtt_host` | str | `192.168.1.43` | broker host |
| `mqtt_port` | u16 | 58443 | broker TCP port |
| `mqtt_ka_sec` | u16 | 20 s | keepalive interval, and the dead-broker detection |

When enabled the unit publishes status variables and log entries under a topic
prefix fixed at build time. Broker credentials and TLS are not supported.

#### Location parameters

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `geo_serial` | u32 | 1 | commit serial |
| `geo_lat_deg` | f32 | 48.07 deg | north positive |
| `geo_lon_deg` | f32 | 1.32 deg | east positive |

Used for solar-position tracking where a site enables it. Set these to the actual
installation, not the nearest city.
