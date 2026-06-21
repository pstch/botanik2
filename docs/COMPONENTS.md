# Config-driven components

The network stack and the sun tracker are built from one shared shape: a small,
non-blocking state machine that the operator configures from the PLC and that the main
loop steps once per tick. Two base classes capture what they have in common, so each
component is just its own state machine plus a few hooks.

## The two base classes

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

`Botanik::ConfigHandler` (`config_handler.h`) is a `Component` that watches one PLC
config block (see @ref md_docs_2PLC__CONFIG). Each block carries a `serial` the PLC
bumps on any edit; `ConfigHandler::step()` reads it and, when it changes, resets the
derived state machine before stepping it. The order is fixed (announce, then
read-serial / reset, then step) and non-overridable - a handler only fills in three
hooks:

- `config_serial()` - return the watched block's serial (e.g. `config()->net.serial`).
- `on_config_changed(now)` - reset the state machine and latch the new config.
- `on_step(now)` - advance the state machine one tick.

## The components

| Component | Watches | Role |
|---|---|---|
| @ref Botanik::Network::NameResolver | (none) | Non-blocking hostname -> IPv4 over Mbed async DNS; a fixed table doubles as the DNS cache. Not config-driven itself - it is shared and holds the live interface. |
| @ref Botanik::Network::NetworkHandler | `net` | Brings the network device up (DHCP / static), owns the interface, and hands it to the resolver while the link is up. |
| @ref Botanik::Network::TimeSyncHandler | `ntp` | SNTP client: resolves a server, runs a UDP exchange, disciplines the RTC. |
| @ref Botanik::Network::MqttClient | `mqtt` | MQTT 3.1.1 publisher: connects to the broker over TCP, keepalive, QoS-0 publish. |
| @ref Botanik::SunPositionTracker | `geo` | Tracks the next sunrise / sunset and the day/night state from the site location (`utils/sun.h`); waits for a valid wall clock. |

## How they fit together

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
