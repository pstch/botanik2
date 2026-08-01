# Networking {#dev_network}

The network stack is optional (`BOTANIK_FEATURE_NETWORK` / `_NTP` / `_MQTT`, all
default off; on for `gh_mlb41`) and lives under `network/`. It is functional on OPTA
over the Mbed network device; on the simulator the `hal/net.cpp` backend is
hermetic - it emulates DNS/NTP/MQTT in-process against a deterministic offline peer,
so the handlers run their real state machines with no host network I/O.

Every handler is **config-driven**: each is a @ref Botanik::Component that reads its
settings by name from the variable bank and watches its parameter group's commit
serial (`group_serial(io, "net")` and friends), re-reading the group and restarting
its state machine when that serial changes. The groups are `net`, `ntp`, `mqtt` and
`geo`. See @ref user_settings for the operator-facing side and @ref dev_vars for how
a group serial is resolved. They share one non-blocking
@ref Botanik::Network::NameResolver.

The DNS resolver is one of only two asynchronous boundaries in the firmware; its
publication contract is in @ref dev_concurrency.

## Components

\startuml "the network stack: config-driven components (each watching its parameter group's commit serial) over one shared resolver"
class Component
class "Alarms::Publisher" as Publisher

Component <|-- NetworkHandler
Component <|-- NtpClient
Component <|-- MqttClient
Publisher <|-- MqttClient

class NameResolver
class MqttSender

NetworkHandler --> NameResolver : brings the device up,\nhands it to
NtpClient ..> NameResolver
NtpClient ..> NetworkHandler
MqttClient ..> NameResolver
MqttClient ..> NetworkHandler
MqttSender --> MqttClient : periodic telemetry via
\enduml

- @ref Botanik::Network::NetworkHandler - brings up the network device (DHCP or the
  static block) from the `net` parameter group and hands it to the resolver. The
  other handlers wait on its link.
- @ref Botanik::Network::NameResolver - non-blocking DNS over Mbed's async
  resolver: a poll-style API plus a small cache, shared by the NTP and MQTT
  clients so a name resolves once.
- @ref Botanik::Network::NtpClient - runs SNTP through the resolver and publishes
  the offset; its synced state and offset feed the `net` command and the wall clock.
- @ref Botanik::Network::MqttClient - a minimal non-blocking MQTT 3.1.1 publisher.
  It is also an `Alarms::Publisher`, so @ref Botanik::Alarms::MqttAlarmNotifier can
  push alarm transitions through it.
- @ref Botanik::Network::MqttSender - periodic telemetry: formats the library base
  set (site-extensible) and publishes it through the `MqttClient` on a cadence.

Each client's live status is borrowed by the UI through the network pointers on
@ref Botanik::SystemContext (see @ref dev_architecture "Architecture").
