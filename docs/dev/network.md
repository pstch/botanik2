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

That `new`/`delete` pair is **the only heap traffic in the firmware**, and it is
forced by the Mbed API rather than chosen. It recurs for the life of the unit: a
SCADA poller that reconnects on its own schedule allocates and frees a
`TCPSocket` every time. The allocation is one fixed size and it is freed before
the next is taken, which is the shape a first-fit allocator handles without
fragmenting - but it is the one place a multi-year uptime could and everything
else in the tree is a fixed array, so it is worth knowing where to look.

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

## Backpressure, and why a full socket is not a dead broker

A QoS-0 publisher may lose a message; it may not lose *half* of one. MQTT frames
back to back on one TCP stream, so the bytes after an unfinished packet are
unreadable until the rest of it goes out. `tcp_send()` on a non-blocking socket
answers with either, and `MqttClient` treats the two differently:

- **Nothing written** (would-block, the socket buffer is full). The message is
  dropped and `publish()` returns false. The session is untouched: a broker
  reading slowly is not a broker that has gone, and the caller - telemetry, the
  log writer, the alarm notifier - already treats false as "dropped".
- **Part written.** The remainder is kept in the client (`_tx`, one packet's
  worth) and pushed at the top of every following beat, ahead of anything new.
  A publish that arrives while a tail is pending is dropped rather than queued.
  A tail that has not moved for `MQTT_TX_STALL_MS` ends the session, because a
  peer that never drains it leaves the stream unusable.

This is what the fast simulator clock exposes. Under `BOTANIK_SIM_CLOCK=fast`
the beat rate against real time is whatever the host manages, so the publisher
and the mock broker meet at rates a plant network never produces; the client
used to end the session on the first short write and reconnect straight back
into the same full socket. `tests/test_mqtt.cpp` drives that against a real
broker socket that is deliberately not read.

The failure has a name. `MqttClient::fault()` records which of the seven ways a
session ended and `status_str()` reports it in place of "error", so the system
page says `broker silent` or `send stalled` rather than sending an operator to a
log that has already scrolled. `TcpSlave` does the same for its listener: the
status page separates `off` (mb_enable) from `bind failed`, and the bind failure
carries `HAL::Network::last_error()` - errno on the simulator, an nsapi code on
the OPTA - because "cannot bind" alone is one number short of the answer.

The simulator's own mock broker (`botanik_sim/network.h`) has the matching
rules: its reassembly buffer is `MQTT_TX_BUF`, so a whole packet of the largest
size the publisher can build always fits, and it reads until the socket is empty
rather than once per beat. A smaller buffer had nothing to do with the packet
that overran it but drop bytes from the middle of the stream, after which every
frame boundary is wrong - the PINGREQs stop being recognised, no PINGRESP comes
back, and the firmware declares a broker that is right here dead.
