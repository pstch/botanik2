# Networking {#dev_network}

The network stack is optional (`BOTANIK_FEATURE_NETWORK` / `_NTP` / `_MQTT`, all
default off; on for `gh_mlb41`) and lives under `network/`. It is functional on OPTA
over the Mbed network device; on the simulator the `hal/net.cpp` backend is
hermetic - it emulates DNS/NTP/MQTT in-process against a deterministic offline peer,
so the handlers run their real state machines with no host network I/O.

Every handler is **config-driven**: each is a @ref Botanik::Component that reads its
parameters by name from the variable bank and watches its parameter group's commit
serial (`group_serial(io, "net")` and friends, which resolves the `<group>_serial`
row), re-reading the group and restarting its state machine when that serial changes.
The groups are `net`, `ntp`, `mqtt` and `geo`. See @ref user_variables for the
operator-facing side and @ref dev_vars for how a group serial is resolved. They share
one non-blocking
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

## Listening, and what the simulator does with it

`HAL::Network` serves as well as connects: `tcp_listen(port)` binds and listens,
`tcp_accept()` takes the next pending connection or returns < 0. Nothing pending
and an error are the same answer deliberately - a server polls the listener every
beat and does nothing either way. An accepted socket is an ordinary one, closed
with `tcp_close()`; on OPTA it is Mbed's own allocation, so the slot table keeps
the pointer and `tcp_close()` deletes it.

The hermetic simulator backend needs two rules to make a server testable without
giving up the guarantee that nothing leaves the host:

- **Privileged ports are shifted.** A process without `CAP_NET_BIND_SERVICE`
  cannot bind below 1024, so anything under it binds at `port + 10000`
  (`HAL::Network::sim_port()`). Modbus 502 is reachable at 10502, which is the
  same arithmetic that already produced `NTP_MOCK_PORT` from 123.
- **A connect to a port we serve is not diverted.** Every outbound connect
  normally lands on the in-process mock broker whatever address it names. A port
  this process is listening on is the exception: that connect is the firmware
  reaching its own server, already in-process, and diverting it would make the
  one thing the sim can genuinely exercise unreachable. The listening ports are
  tracked beside their fds so closing a listener retires the exemption.
