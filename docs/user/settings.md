# Settings {#user_settings}

A setting is just a variable with the parameter role: editable, range-checked, and
saved. Everything else about it - name, type, unit, description - works exactly as in
@ref user_variables.

## Editing one

Two equivalent routes:

- **PARAMS page** - select, `ENTER`, type, `ENTER`. The panel shows the minimum,
  maximum and description before you commit. See @ref user_pages.
- **`set` from the shell** - `set ntp_poll_s 128`.

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
settings never invalidates the others.

## Applying a group of settings

Network, NTP, MQTT and geo settings are grouped, and each group has a **commit
serial** - a parameter named after the group itself (`net`, `ntp`, `mqtt`, `geo`).
The component that consumes a group re-reads all of it, and restarts, when that
serial changes.

@warning Editing a parameter does **not** apply it. The value is saved immediately,
but the component keeps running on what it read last. You must bump the group's
serial to make it take effect.

So a change is always two steps: edit, then commit.

```
botanik> set net_dhcp 0
botanik> set net_ip 192.168.1.40
botanik> set net_netmask 255.255.255.0
botanik> set net_gateway 192.168.1.1
botanik> set net_dns1 192.168.1.1
botanik> set net 2                  # <- apply the whole group
```

Any new value will do, as long as it differs from the current one; incrementing is
the convention. Staging the whole group before committing is the point of the design:
a static IP block is only coherent once every field is set, and this way the interface
restarts once rather than five times on its way through a half-configured state.

## Network settings

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `net` | u32 | 1 | commit serial - bump to re-apply the group |
| `net_dhcp` | bool | 1 | 1 = DHCP, 0 = static |
| `net_ip` | str | | static IPv4 address |
| `net_netmask` | str | | static subnet mask |
| `net_gateway` | str | | default gateway |
| `net_dns1` | str | | primary DNS server |
| `net_dns2` | str | | secondary DNS server (may be empty) |

The static fields are ignored while `net_dhcp` is 1. `net` reports what the
interface actually ended up with, which is how you confirm a lease.

## Time settings (NTP)

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ntp` | u32 | 1 | commit serial |
| `ntp_enabled` | bool | 1 | enable time sync |
| `ntp_poll_s` | u16 | 64 s | poll interval |
| `ntp_server1` | str | `pool.ntp.org` | primary server (host or IP) |
| `ntp_server2` | str | | secondary server (may be empty) |

`ntpsync` forces a resync without waiting for the next poll. The status-bar clock
and every log timestamp follow this.

## Telemetry settings (MQTT)

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `mqtt` | u32 | 1 | commit serial |
| `mqtt_enabled` | bool | 1 | enable publishing |
| `mqtt_broker` | str | `broker.local` | broker host |
| `mqtt_port` | u16 | 1883 | broker TCP port |
| `mqtt_keepalive_s` | u16 | 60 s | keepalive interval |

When enabled the unit publishes status variables and log entries under a topic
prefix fixed at build time. Broker credentials and TLS are not supported.

## Location settings

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `geo` | u32 | 1 | commit serial |
| `geo_latitude` | f32 | 48.8566 deg | north positive |
| `geo_longitude` | f32 | 2.3522 deg | east positive |

Used for solar-position tracking where a site enables it. Set these to the actual
installation, not the nearest city.
