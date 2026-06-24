# PLC configuration variables and components

How the PLC hands runtime configuration to the host firmware (network, NTP, MQTT
settings plus site location for sunrise/sunset), and the components that consume it.

This is *configuration* an installer sets per site without recompiling. The PLC
program is its natural home (that is where the installer already works, and its
retained variables survive a reboot), so the **PLC is the configuration authority**;
it travels in the PLC->host (`PlcOutput`) region like any other PLC output.

The config block is **generic**: it lives in `libbotanik` (`plcio/config.h`), not in
a site payload, so the components are written once against one layout. The first half
of this page is the config block (the wire contract); the second is the components
that consume it.

## Where it sits

The config block is a library-fixed section of `PlcOutput<Data>` (`plcio/vars.h`).
In layout order the generic `conf` block sits between the log ring and the site
payload:

\startuml "PlcOutput layout: the generic conf block sits between the log ring and the site payload"
class "PlcOutput<OutputData>" as PO {
  sync : txSeqNum, rxAckNum
  logs : writeIndex, logBuffer[N]
  conf : net, ntp, mqtt, geo  (generic, library-fixed - this doc)
  data : site payload
}
\enduml

It is snapshotted in the *same* RX transaction as the payload, under the same seqnum
handshake (see @ref md_docs_2PLC__SYNC); consumers read the stable local copy through
`PlcSyncContext::config()`, never the shared region directly.

The host never writes this block; only the PLC does. There is no host->PLC config
acknowledgement - a consumer reflects what it applied through the status it already
reports (link state, NTP-synced flag, and so on).

## The blocks

Each block leads with a 32-bit `serial` (see "Change detection" below). Addresses are
text, not packed integers (see "Addresses").

### net - network interface

| Field | Type | Meaning |
|---|---|---|
| `serial`  | `uint32_t`  | bumped by the PLC on any change to this block |
| `dhcp`    | `uint8_t`   | 1 = DHCP (the static fields below are ignored), 0 = static |
| `ip`      | `HostStr_t` | static IPv4 literal, used when `dhcp == 0` |
| `netmask` | `HostStr_t` | static netmask literal (dotted-decimal) |
| `gateway` | `HostStr_t` | default-gateway literal |
| `dns1`    | `HostStr_t` | primary DNS server (the resolver's upstream) |
| `dns2`    | `HostStr_t` | secondary DNS server, empty = none |

### ntp - NTP client

| Field | Type | Meaning |
|---|---|---|
| `serial`  | `uint32_t`  | bumped by the PLC on any change to this block |
| `enabled` | `uint8_t`   | 1 = run the NTP client and discipline the RTC |
| `poll_s`  | `uint16_t`  | poll interval, seconds (0 = firmware default) |
| `server1` | `HostStr_t` | hostname or IPv4 literal |
| `server2` | `HostStr_t` | empty = none |

### mqtt - MQTT client

| Field | Type | Meaning |
|---|---|---|
| `serial`      | `uint32_t`  | bumped by the PLC on any change to this block |
| `enabled`     | `uint8_t`   | 1 = connect to the broker |
| `port`        | `uint16_t`  | broker TCP port (0 = firmware default, 1883) |
| `keepalive_s` | `uint16_t`  | MQTT keepalive, seconds (0 = firmware default) |
| `broker`      | `HostStr_t` | hostname or IPv4 literal |

The MQTT **client id** is *not* on the wire: it is derived from `Config::SITE_NAME` at
compile time. There are no credential or TLS fields - the brokers in scope are
unauthenticated and plaintext; adding either is a coordinated layout change here plus
on the PLC side.

### geo - site location

The site's geographical position, used by the base library to compute sunrise and
sunset (e.g. for window / thermal-screen scheduling). Integer-scaled - no floats on
the wire - so latitude and longitude use the common "E7" encoding (degrees * 1e7).

| Field | Type | Meaning |
|---|---|---|
| `serial`        | `uint32_t` | bumped by the PLC on any change to this block |
| `latitude_e7`   | `int32_t`  | latitude, degrees * 1e7 (north +, south -); -90e7..90e7 |
| `longitude_e7`  | `int32_t`  | longitude, degrees * 1e7 (east +, west -); -180e7..180e7 |

No timezone field: the device runs in UTC throughout (RTC is UTC from NTP, solar math
is timezone-independent - longitude alone fixes solar noon). Local-time presentation
belongs off-device. No elevation either - its effect on sunrise/sunset is negligible.

## Change detection: the per-block serial

Each block carries its own `serial`, so a consumer reacts to its own config and not
the others. A consumer latches the last serial it applied and each tick compares it to
the live one: equal means nothing to do (the common case, no re-resolve every tick);
greater means re-apply and store the new serial.

It is a free-running counter, not a dirty flag: the PLC just increments it after
editing the block, and the host needs no handshake to clear it. The serial leads its
struct so a consumer can poll it through a word-aligned `uint32_t*`, mirroring the log
cursors. The watch-and-reset is not re-implemented per consumer: it lives in the
`Botanik::ConfigHandler` base class (below).

## Addresses

Every address is a `HostStr_t` - a fixed `char` buffer holding a hostname
(`pool.ntp.org`) or a dotted-decimal IPv4 literal (`192.168.1.10`), NUL-terminated;
empty (`[0] == 0`) means "unset". Length is `Config::PLC_HOST_STR_LEN` (default 64;
**must match the PLC side**).

Text was chosen over packed octets so the operator can enter a server by name. The
cost is resolution: the host parses the buffer as a literal (instant) and otherwise
runs a DNS lookup. That lookup must not block the main loop, so it is the job of a
non-blocking resolver, `Botanik::Network::NameResolver` (`network/resolver.h`):
poll-style `resolve()` over Mbed's async DNS, backed by a fixed table that doubles as
the DNS cache. Pure-literal fields (static `ip` / `netmask` / `gateway`) never touch
DNS.

## PLC-side setup

The host pins the layout with `static_assert`s (`plcio/config.h`,
`libbotanik_sizes.h`); the **PLC program must mirror it byte-for-byte**, exactly as it
already does for the log protocol and the site payload. Steps for the PLC IDE:

1. **Match `PLC_HOST_STR_LEN`.** Each address is a `BYTE[PLC_HOST_STR_LEN]` (default
   64) holding a NUL-terminated ASCII string. To change the length, override
   `BOTANIK_PLC_HOST_STR_LEN` and use the same value on both sides (an ABI value, like
   the `LOG_PLC_*` sizes).
2. **Declare the four structs in order** - `net`, `ntp`, `mqtt`, `geo` - after the log
   ring and before the site payload (per the diagram), keeping the explicit `_pad*`
   bytes so every member lands on its natural offset; blocks are `net` 328 B, `ntp`
   136 B, `mqtt` 76 B, `geo` 12 B (552 B total) at the default string length.
3. **Lead each block with its `DWORD serial`.**
4. **Write strings as NUL-terminated ASCII**, dotted-decimal for IPv4 literals; an
   all-zero buffer means "unset".
5. **Bump the right `serial`** (any increment) *after* editing a block's fields.
   Partially-written config is never half-applied: the seqnum handshake holds the whole
   region steady until the host acks (see @ref md_docs_2PLC__SYNC).

On OPTA the combined struct is checked against the PLC IDE's generated type by the
sketch's `static_assert`, so a layout mismatch fails the build rather than corrupting
config at runtime. In the simulator there is no PLC IDE: the harness seeds an example
config block directly into the shared region (`botanik_sim/main.cpp`).

## The components that consume it

The network stack and the sun tracker share one shape: a small non-blocking state
machine the operator configures from the PLC (the blocks above), stepped once per
tick. Two base classes capture the commonality, so each component is just its state
machine plus a few hooks.

- `Botanik::Component` (`component.h`) gives a one-shot first-`step()` DEBUG/BOOT
  announce (`<name> in loop`), confirming the component is actually in the loop
  (construction order says nothing about loop inclusion); compiled out in release. The
  core loop components announce directly; config-driven ones get it through
  `ConfigHandler`.
- `Botanik::ConfigHandler` (`config_handler.h`) is a `Component` that watches one
  config block: each `step()` reads its `serial` (see *Change detection*) and, on
  change, resets the derived state machine before stepping it. The order
  (announce -> read-serial/reset -> step) is fixed; a handler fills three hooks
  (`config_serial`, `on_config_changed`, `on_step`).

### The component table

| Component | Watches | Role |
|---|---|---|
| @ref Botanik::Network::NameResolver | (none) | Non-blocking hostname -> IPv4 over Mbed async DNS; a fixed table doubles as the DNS cache. Not config-driven itself - it is shared and holds the live interface. |
| @ref Botanik::Network::NetworkHandler | `net` | Brings the network device up (DHCP / static), owns the interface, and hands it to the resolver while the link is up. |
| @ref Botanik::Network::NtpClient | `ntp` | SNTP client: resolves a server, runs a UDP exchange, disciplines the RTC. |
| @ref Botanik::Network::MqttClient | `mqtt` | MQTT 3.1.1 publisher: connects to the broker over TCP, keepalive, QoS-0 publish. |
| @ref Botanik::Sun::SunPositionTracker | `geo` | Tracks the next sunrise / sunset and the day/night state from the site location (`sun/sun.h`); waits for a valid wall clock. Gated by `BOTANIK_FEATURE_SUN`. |

### How they fit together

```
┌──────────────────────────────────────────┐
│ NetworkHandler   (watches: net)           │
│ device bring-up (DHCP / static)           │
└──────────────────────────────────────────┘
     │ set_interface (on link up)
     ▼
┌──────────────────────────────────────────┐
│ NameResolver   (holds the live interface) │
│ resolve(): non-blocking DNS + cache       │
└──────────────────────────────────────────┘
     │
     ├──▶ NtpClient  (watches: ntp)   UDP / SNTP
     └──▶ MqttClient       (watches: mqtt)  TCP / MQTT publish
```

`NtpClient` and `MqttClient` each resolve their server / broker name through the
resolver and open their own socket on its interface; they back off on failure and
restart when their config block changes. `SunPositionTracker` needs no network, only a
valid wall clock, so it follows a `NtpClient` sync.

Targets: the Mbed socket work (DNS, UDP, TCP, device bring-up) is OPTA-only and
guarded; on the simulator there is no stack, so the resolver serves IPv4 literals only
and the network handler reports the link up immediately.
