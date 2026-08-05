# Parameters {#user_params}

A parameter is just a variable with the parameter role: editable, range-checked, and
saved. Everything else about it - name, type, unit, description - works exactly as in
@ref user_variables. The PARAMS page and the `set` command are the two ways to reach
one, and both call them parameters; nothing in the firmware calls them "settings".

## Editing one

Two equivalent routes:

- **PARAMS page** - select, `ENTER`, type, `ENTER`. The panel shows the minimum,
  maximum and description before you commit. See @ref user_pages.
- **`set` from the shell** - `set ntp_poll_sec 1800`.

Either way the value is parsed, checked against the parameter's declared band, and
written to persistent storage. Three outcomes:

| Result | Meaning |
|---|---|
| `<name> = <value>` | done, and saved |
| `set: invalid value for <name>` | rejected: wrong format or out of range. Nothing changed |
| `<name> = <value>  (NOT SAVED: storage refused it)` | **applied but not saved.** The value is live now and will be lost on reboot |

That third case is a storage fault, not a bad value - it also raises the `FLASH_FLT`
alarm. See @ref user_alarms.

`set` only works on parameters. Pointing it at a sensor or an output is refused with
a hint to use `override` instead: writing those would simply be overwritten on the
next scan.

Storage is one key per parameter, named after it, so adding, removing or reordering
parameters never invalidates the others.

## Applying a group of parameters

Network, NTP, MQTT and geo parameters are grouped, and each group has a **commit
serial** - a parameter named `<group>_serial`, so it files under its own group like
every other row in it. The component that consumes a group re-reads all of it, and
restarts, when that serial changes.

@warning Editing a parameter does **not** apply it. The value is saved immediately,
but the component keeps running on what it read last. You must bump the group's
serial to make it take effect.

So a change is always two steps: edit, then commit.

```
botanik> set net_dhcp 0
botanik> set net_ip 192.168.1.40
botanik> set net_mask 255.255.255.0
botanik> set net_gw 192.168.1.1
botanik> set net_dns1 192.168.1.1
botanik> set net_serial 2           # <- apply the whole group
```

Any new value will do, as long as it differs from the current one; incrementing is
the convention. Staging the whole group before committing is the point of the design:
a static IP block is only coherent once every field is set, and this way the interface
restarts once rather than five times on its way through a half-configured state.

## The groups the library itself reads

@note What follows is **not the parameter list of your unit.** These four groups are
the ones the library's own components consume, so they exist on every build with the
matching subsystem, and they are the only ones this manual can name. A greenhouse
declares its own parameters in its site table - setpoints, dwell times, calibration
trims - and those are documented with the site, not here.

`list` with a prefix and the PARAMS page are the authoritative list of what your unit
actually has: `list irr_` shows the irrigation parameters whether or not any page of
this manual has heard of them.

The names below are fixed, but every **default** is the site's - the values shown are
the reference site `gh_mlb41`'s.

## Network parameters

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

## Time parameters (NTP)

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ntp_serial` | u32 | 1 | commit serial |
| `ntp_enable` | bool | 1 | enable time sync |
| `ntp_poll_sec` | u32 | 3600 s | poll interval |
| `ntp_srv1` | str | `0.fr.pool.ntp.org` | primary server (host or IP) |
| `ntp_srv2` | str | `1.fr.pool.ntp.org` | secondary server (empty = no fallback) |

`ntpsync` forces a resync without waiting for the next poll. The status-bar clock
and every log timestamp follow this.

## Telemetry parameters (MQTT)

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `mqtt_serial` | u32 | 1 | commit serial |
| `mqtt_enable` | bool | 1 | enable publishing |
| `mqtt_host` | str | `192.168.1.43` | broker host |
| `mqtt_port` | u16 | 58443 | broker TCP port |
| `mqtt_ka_sec` | u16 | 20 s | keepalive interval, and the dead-broker detection |

When enabled the unit publishes status variables and log entries under a topic
prefix fixed at build time. Broker credentials and TLS are not supported.

## Location parameters

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `geo_serial` | u32 | 1 | commit serial |
| `geo_lat_deg` | f32 | 48.07 deg | north positive |
| `geo_lon_deg` | f32 | 1.32 deg | east positive |

Used for solar-position tracking where a site enables it. Set these to the actual
installation, not the nearest city.
