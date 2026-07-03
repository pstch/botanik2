# Networking

The network stack is optional (`BOTANIK_FEATURE_NETWORK` / `_NTP` / `_MQTT`,
default off; on for `gh_mlb41`) and lives under `network/`. It is functional on
OPTA over the Mbed network device; on the simulator the `hal/net.cpp` backend is
hermetic - it emulates DNS/NTP/MQTT in-process against a deterministic offline
peer, so the handlers run their real state machines with no host network I/O.

Every handler is **config-driven**: each derives @ref Botanik::ConfigHandler
(itself a @ref Botanik::Component), so it resets and restarts whenever the
operator bumps its PLC config block's serial - the network / NTP / MQTT / geo
blocks in @ref md_docs_2PLC__CONFIG "the PLC config layout". They share one
non-blocking @ref Botanik::Network::NameResolver.

## Components

\startuml "the network stack: config-driven handlers (each reacting to its PLC config block) over one shared resolver"
class Component
class ConfigHandler
class "Alarms::Publisher" as Publisher

Component <|-- ConfigHandler
ConfigHandler <|-- NetworkHandler
ConfigHandler <|-- NtpClient
ConfigHandler <|-- MqttClient
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

- @ref Botanik::Network::NetworkHandler - brings up the network device (DHCP)
  from the network config block and hands it to the resolver. The other handlers
  wait on its link.
- @ref Botanik::Network::NameResolver - non-blocking DNS over Mbed's async
  resolver: a poll-style API plus a small cache, shared by the NTP and MQTT
  clients so a name resolves once.
- @ref Botanik::Network::NtpClient - runs SNTP through the resolver and publishes
  the offset; its synced state and offset feed the STATUS page and the wall clock.
- @ref Botanik::Network::MqttClient - a minimal non-blocking MQTT 3.1.1 publisher.
  It is also an `Alarms::Publisher`, so @ref Botanik::Alarms::MqttAlarmNotifier can
  push alarm transitions through it.
- @ref Botanik::Network::MqttSender - periodic telemetry: formats the library base
  set (site-extensible) and publishes it through the `MqttClient` on a cadence.

Each client's live status is borrowed by the UI through the network pointers on
@ref Botanik::SystemContext (see @ref md_docs_2CONTEXTS "Contexts").
