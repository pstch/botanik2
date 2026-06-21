# PLC configuration variables and components

How the PLC hands runtime configuration to the host firmware, and the components that
consume it: the network, NTP, and MQTT settings the host's network stack, NTP client,
and MQTT client need, plus the site's geographical location for the sunrise/sunset
calculation.

Until now the host read only *live data* from the PLC (sensor values, alarms, logs).
These components are different: they need *configuration* that an installer sets per
site without recompiling the firmware. The PLC program is the natural home for that -
it is where the installer already works, and its retained variables survive a reboot -
so the **PLC is the configuration authority** and the host consumes it. The config
therefore travels in the PLC->host (`PlcOutput`) region, like any other PLC output.

This config block is **generic**: it lives in `libbotanik` (`plcio/config.h`), not in
a site payload. Every site and target gets the identical layout, so the components can
be written once against it. The first half of this page is the **config block** (the
wire contract); the second is the **components** that consume it.

## Where it sits

The config block is a library-fixed section of `PlcOutput<Data>`, spliced in between
the log ring and the site payload (`plcio/vars.h`):

\dot "PlcOutput<OutputData>: the config block sits between the log ring and the site payload"
digraph plc_output {
  bgcolor="transparent";
  rankdir=LR; nodesep=0.5;
  node [shape=plaintext, fontname="Helvetica", fontsize=10];

  layout [label=<
    <table border="0" cellborder="1" cellspacing="0" cellpadding="6" color="#9e9e9e">
      <tr><td colspan="2" bgcolor="#37474f"><font color="#ffffff"><b>PlcOutput&lt;OutputData&gt;</b> &#160; (PLC &#8594; host)</font></td></tr>
      <tr><td bgcolor="#d6e4f0"><b>sync</b></td><td bgcolor="#d6e4f0" align="left">txSeqNum &#160; rxAckNum</td></tr>
      <tr><td bgcolor="#fdebcf"><b>logs</b></td><td bgcolor="#fdebcf" align="left">writeIndex<br align="left"/>logBuffer[N]</td></tr>
      <tr><td bgcolor="#d7ecd9" rowspan="4" port="cfg"><b>conf</b></td><td bgcolor="#d7ecd9" align="left">net &#160; <font color="#666666">(network)</font></td></tr>
      <tr><td bgcolor="#d7ecd9" align="left">ntp &#160; <font color="#666666">(NTP client)</font></td></tr>
      <tr><td bgcolor="#d7ecd9" align="left">mqtt &#160; <font color="#666666">(MQTT client)</font></td></tr>
      <tr><td bgcolor="#d7ecd9" align="left">geo &#160; <font color="#666666">(location)</font></td></tr>
      <tr><td bgcolor="#eeeeee"><b>data</b></td><td bgcolor="#eeeeee" align="left"><font color="#666666">&lt;site payload&gt;</font></td></tr>
    </table>
  >];

  note [shape=note, style=filled, fillcolor="#eafaef", color="#4caf50", fontsize=9,
        fontname="Helvetica", label="generic,\nlibrary-fixed\n(this doc)"];
  note -> layout:cfg [color="#4caf50", arrowsize=0.6];
}
\enddot

It is snapshotted in the *same* RX transaction as the payload: `PlcSyncHandler`'s
`_rx_memcpy()` copies `conf` into a local buffer right beside `data`, under the same
seqnum handshake (see @ref md_docs_2PLC__SYNC). Consumers read the stable local copy
through `PlcSyncContext::config()`, never the shared region directly.

The host never writes this block; only the PLC does. There is no host->PLC config
acknowledgement - a consumer simply reflects what it applied through the existing
status it already reports (link state, NTP-synced flag, and so on).

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
| `tz_offset_min` | `int16_t`  | local time offset from UTC, minutes (e.g. +60 = CET) |

The offset converts the computed UTC sunrise/sunset into the local "minutes since
midnight" the displays expect. Daylight-saving is folded into `tz_offset_min` by the
PLC for now (a proper DST refinement is future work). Elevation is not carried - its
effect on sunrise/sunset is negligible here.

> Note: where a site currently has the PLC compute `sunrise_min` / `sunset_min` in its
> own payload (e.g. `gh_mlb41`), moving the calculation into the base library from this
> `geo` block lets a site drop those payload fields. That is a per-site change, separate
> from adding the config variables here.

## Change detection: the per-block serial

Each block carries its own `serial`, so a consumer reacts to its own config and not
the others. A consumer latches the last serial it applied and, each tick, compares it
to the live one:

- equal -> nothing to do (the common case; no re-resolve, no re-bind every tick).
- greater -> the PLC changed this block; re-apply it and store the new serial.

This is why the field is a free-running counter, not a dirty flag: the PLC just
increments it after editing the block, and the host needs no handshake to clear it.
The serial leads its struct so a consumer could poll it through a word-aligned
`uint32_t*`, mirroring the log cursors.

The watch-and-reset is not re-implemented per consumer: it lives in the
`Botanik::ConfigHandler` base class, described under *The components that consume it*
below.

## Addresses

Every address is a `HostStr_t` - a fixed `char` buffer holding either a hostname
(`pool.ntp.org`) or a dotted-decimal IPv4 literal (`192.168.1.10`), NUL-terminated;
an empty string (`[0] == 0`) means "unset". The buffer length is
`Config::PLC_HOST_STR_LEN` (default 64; **must match the PLC side**).

Text was chosen over packed octets so the operator can enter a broker / NTP server by
name. The cost is that a name has to be resolved: the host first tries to parse the
buffer as a literal (instant), and otherwise runs a DNS lookup. That lookup must not
block the main loop, so it is the job of a dedicated non-blocking resolver,
`Botanik::Network::NameResolver` (`network/resolver.h`): a poll-style `resolve()` over
Mbed's async DNS, backed by a fixed table that doubles as the DNS cache. Pure-literal
fields (the static `ip` / `netmask` / `gateway`) always parse and never touch DNS.

## PLC-side setup

The host pins the layout with `static_assert`s (`plcio/config.h`,
`libbotanik_sizes.h`); the **PLC program must mirror it byte-for-byte**, exactly as it
already does for the log protocol and the site payload. Steps for the PLC IDE:

1. **Match `PLC_HOST_STR_LEN`.** Each address is a `BYTE[PLC_HOST_STR_LEN]` (default
   64) holding a NUL-terminated ASCII string. If you change the length, override
   `BOTANIK_PLC_HOST_STR_LEN` in `sites/<name>/config.h` and use the same value on
   both sides - it is build-wide on the host (an ABI value, like the `LOG_PLC_*`
   sizes).
2. **Declare the four structs in order** - `net`, `ntp`, `mqtt`, then `geo` - inside
   the PLC's output structure, placed **after the log ring and before the site
   payload**, matching the diagram above. Keep the field order and the explicit
   padding bytes (`_pad*`) so every member lands on its natural offset; the blocks are
   `net` 328 B, `ntp` 136 B, `mqtt` 76 B, `geo` 16 B (556 B total) at the default
   string length.
3. **Lead each block with its `DWORD serial`.**
4. **Write strings as NUL-terminated ASCII**, dotted-decimal for IPv4 literals
   (`"192.168.1.10"`). Leave a buffer all-zero to mean "unset".
5. **Bump the right `serial`** (any increment) *after* you finish editing a block's
   fields. The host applies the block on the next increment it sees; partially-written
   config is never half-applied because the seqnum handshake holds the whole region
   steady until the host acks (see @ref md_docs_2PLC__SYNC).

On OPTA the combined struct is checked against the PLC IDE's generated type by the
sketch's `static_assert` (`sizeof(PLCSharedVarsOutput_t) == sizeof(PlcOutput<...>)`),
so a layout mismatch fails the build rather than corrupting config at runtime.

In the simulator there is no PLC IDE: the harness seeds an example config block
directly into the shared region (`botanik_sim/main.cpp`), standing in for the
coprocessor that would publish it on hardware.

## The components that consume it

The network stack and the sun tracker are built from one shared shape: a small,
non-blocking state machine that the operator configures from the PLC (the blocks
above) and that the main loop steps once per tick. Two base classes capture what they
have in common, so each component is just its own state machine plus a few hooks.

### Component - first-step announce

`Botanik::Component` (`component.h`) gives a component one job for free: the first time
it is stepped, in a DEBUG build, it logs a single `DEBUG` / `BOOT` line (`<name> in
loop`). That confirms the component is actually wired into the main event loop - a
component that is constructed but never stepped (a wiring bug) stays silent, and its
missing line is the tell. The announce fires on the first `step()`, never in the
constructor: construction order says nothing about loop inclusion. With `DEBUG`
undefined (the OPTA release build) it compiles down to setting a flag.

A component derives `Component` and calls `announce_step(logger, name)` at the top of
its `step()`. The core loop components do this directly (scheduler, PLC sync, PLC log
reader, health); the config-driven ones get it through `ConfigHandler`.

### ConfigHandler - reset on config change

`Botanik::ConfigHandler` (`config_handler.h`) is a `Component` that watches one of the
config blocks above. Each block carries a `serial` the PLC bumps on any edit (see
*Change detection* above); `ConfigHandler::step()` reads it and, when it changes,
resets the derived state machine before stepping it. The order is fixed (announce,
then read-serial / reset, then step) and non-overridable - a handler only fills in
three hooks:

- `config_serial()` - return the watched block's serial (e.g. `config()->net.serial`).
- `on_config_changed(now)` - reset the state machine and latch the new config.
- `on_step(now)` - advance the state machine one tick.

### The component table

| Component | Watches | Role |
|---|---|---|
| @ref Botanik::Network::NameResolver | (none) | Non-blocking hostname -> IPv4 over Mbed async DNS; a fixed table doubles as the DNS cache. Not config-driven itself - it is shared and holds the live interface. |
| @ref Botanik::Network::NetworkHandler | `net` | Brings the network device up (DHCP / static), owns the interface, and hands it to the resolver while the link is up. |
| @ref Botanik::Network::TimeSyncHandler | `ntp` | SNTP client: resolves a server, runs a UDP exchange, disciplines the RTC. |
| @ref Botanik::Network::MqttClient | `mqtt` | MQTT 3.1.1 publisher: connects to the broker over TCP, keepalive, QoS-0 publish. |
| @ref Botanik::SunPositionTracker | `geo` | Tracks the next sunrise / sunset and the day/night state from the site location (`utils/sun.h`); waits for a valid wall clock. |

### How they fit together

`NetworkHandler` is the root: it owns the device and, once the link is up, hands the
live interface to the shared `NameResolver`. Everything that needs the network reaches
it through the resolver, which is both the DNS cache and the holder of the interface:

\dot "NetworkHandler hands the live interface to the shared NameResolver"
digraph components {
  bgcolor="transparent"; rankdir=TB; nodesep=0.4; ranksep=0.5;
  node [shape=box, style=rounded, fontname="Helvetica", fontsize=10, margin="0.1,0.05"];
  edge [arrowsize=0.7];

  nh [label="NetworkHandler\n(device bring-up)"];
  nr [label="NameResolver\n(interface + DNS)", style="rounded,filled", fillcolor="#d6e4f0"];
  ts [label="TimeSyncHandler\n(UDP / NTP)"];
  mq [label="MqttClient\n(TCP / MQTT)"];
  ot [label="(other users)", style="rounded,dashed", fontcolor="#777777", color="#bbbbbb"];

  nh -> nr [label="set_interface\n(on link up)", fontsize=8];
  nr -> ts;
  nr -> mq;
  nr -> ot;
}
\enddot

`TimeSyncHandler` and `MqttClient` each resolve their server / broker name through the
resolver and open their own socket on the resolver's interface; they back off and
retry on failure and restart whenever their config block changes. `SunPositionTracker`
needs no network, only a valid wall clock - so it naturally follows a `TimeSyncHandler`
sync.

Targets: the Mbed socket work (DNS, UDP, TCP, device bring-up) is OPTA-only and
guarded; on the simulator there is no stack, so the resolver serves IPv4 literals only
and the network handler reports the link up immediately, leaving the rest idle.
