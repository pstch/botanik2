# Instantiate the network / NTP / MQTT stack

Date: 2026-06-22
Branch: `instantiate-network`

## Goal

Turn on and fully wire the network stack for the reference site `gh_mlb41`: the
link bring-up (`NetworkHandler`), DNS (`NameResolver`), the SNTP client
(`NtpClient`, renamed from `TimeSyncHandler`), and the MQTT client (`MqttClient`),
so the firmware brings up the link, syncs the RTC over SNTP, connects to a broker,
and publishes alarm transitions over MQTT.

## Current state (what already exists)

The firmware scaffolding is present but gated off (`BOTANIK_FEATURE_NETWORK/_NTP/
_MQTT` default 0 in `libbotanik_features.h`):

- `BotanikFirmware` already declares the members, constructs them, runs their
  `step()` in `loop()`, and exposes accessors - all behind the feature gates.
- The generic PLC config struct (`plcio/config.h`) already has `net` / `ntp` /
  `mqtt` blocks (`PlcNetConfig_t` / `PlcNtpConfig_t` / `PlcMqttConfig_t`), each
  led by a `serial` the consumer latches.
- `NetworkHandler`, `NameResolver`, `TimeSyncHandler` (SNTP), and `MqttClient`
  exist as non-blocking, `ConfigHandler`-driven `step()` state machines, already
  refactored onto `HAL::Net` (the simulator backend uses real BSD sockets), and
  unit-tested.

So this is wiring + a notifier consolidation + a rename, not a from-scratch
client implementation.

Three gaps block turning the stack on:

1. **Stale ctor wiring.** A prior refactor gave the SNTP and MQTT clients a 4th
   ctor arg (a `const NetworkHandler*` link gate). The gated ctor calls in
   `firmware.h` still pass 3 args, so the stack does not compile with the flags
   on.
2. **The alarm notifier is a stub.** `MqttAlarmNotifier` (owned by
   `HealthMonitor`) formats the topic/payload but its `publish()`/`_send()` are
   no-ops with no handle to a transport.
3. **The simulator mock PLC never publishes the config blocks**, so the sim would
   not exercise the stack.

## Design

### 1. Enable the features (`sites/gh_mlb41/config.h`)

Add, alongside the site's other config overrides:

```c
#define BOTANIK_FEATURE_NETWORK 1
#define BOTANIK_FEATURE_NTP     1
#define BOTANIK_FEATURE_MQTT    1
```

`libbotanik` defaults stay off; only the reference site opts in. Both the sim and
the OPTA build of `gh_mlb41` now compile and run the stack. (Dependency guards in
`libbotanik_features.h` are already satisfied: NTP/MQTT require NETWORK, NETWORK
requires PLC, and `gh_mlb41` uses PLC.)

### 2. Fix the stale ctor wiring (`firmware.h`)

The clients gate-construct with the link gate:

```cpp
, _ntp(_plc_handler.context(), &_logger, &_resolver, &_network)
, _mqtt(_plc_handler.context(), &_logger, &_resolver, &_network)
```

`_network` is declared before both, so member-init order is correct.

### 3. Consolidate the alarm notifier (one notifier + a Publisher seam)

Today there are two layers: an abstract `AlarmNotifier` (in `alarms/alarms.h`,
what `AlarmModel` calls) and a concrete `MqttAlarmNotifier` (in
`alarms/notifier.h`, stub transport). Collapse to a single notifier whose only
variation point is the transport:

- **New `alarms/publisher.h`:** an abstract transport seam.
  ```cpp
  namespace Botanik { namespace Alarms {
      struct Publisher {
          virtual ~Publisher() {}
          virtual void publish(const char* topic, const char* payload) = 0;
      };
  }}
  ```
- **`alarms/notifier.h`:** a single concrete `AlarmNotifier`. It holds the topic
  prefix and a `Publisher* _pub` (may be null = no-op), implements `notify_on` /
  `notify_off` to format `"<prefix>/alarms/<CODE>"` + `"ON"`/`"OFF"` and forward
  to `_pub->publish(...)` when set. `set_publisher(Publisher*)` wires the
  transport. `MqttAlarmNotifier` is deleted.
- **`MqttClient`** implements `Alarms::Publisher`: `publish(topic, payload)`
  overrides to call its existing `publish(topic, payload, /*retain=*/false)`.
- **`HealthMonitor`** owns the `AlarmNotifier` (member renamed `_mqtt` ->
  `_notifier`) and exposes `set_alarm_publisher(Publisher*)` forwarding to it.
- **`BotanikFirmware::setup()`** wires the transport once, in the base (not the
  site):
  ```cpp
  #if BOTANIK_FEATURE_MQTT
      _health.set_alarm_publisher(&_mqtt);
  #endif
  ```

Direction of dependency: `alarms/` depends only on the `Publisher` interface;
`network/` (the `MqttClient`) depends on `alarms/publisher.h`. To keep
`AlarmModel` free of a concrete-notifier include cycle, `alarms.h` references the
notifier by forward declaration / pointer and the dispatch that calls
`notify_on/off` stays out-of-line (in `alarms.cpp`); the implementation plan
pins the exact placement after checking whether those calls are currently inline.

### 4. Rename `TimeSyncHandler` -> `NtpClient`

A mechanical rename for symmetry with `MqttClient`, across `network/network.h`,
`network/network.cpp`, the firmware member (`_time_sync` -> `_ntp`) and accessor
(`time_sync()` -> `ntp()`), the SNTP unit test, and any doc references. No
behavior change.

### 5. Simulator mock PLC publishes the config blocks (`sites/gh_mlb41/sim.h`)

The mock PLC currently fills only status telemetry (`net_status`, `ntp_synced`,
...), not the config region the handlers read. Populate the `PlcOutputConfig_t`
blocks once (each `serial = 1` so the handler latches on the first beat and does
not re-apply):

- `net`: `dhcp = 1` -> the link comes up via the sim's BSD-socket backend.
- `ntp`: `enabled = 1`, `server1 = "pool.ntp.org"` -> real SNTP sync.
- `mqtt`: `enabled = 1`, `broker = "test.mosquitto.org"`, `port = 1883` -> real
  broker connect; alarm transitions publish under `Config::MQTT_TOPIC_PREFIX`.

So `botanik_sim` genuinely brings up the link, syncs time, connects, and
publishes on every run. The two public hosts are the chosen real targets; they
live in one place (the sim's config init) and are easy to change.

### 6. Tests & verification

- **Unit:** a notifier test that drives `AlarmNotifier::notify_on/off` against a
  mock `Publisher` and asserts the exact topic (`<prefix>/alarms/<CODE>`) and
  payload (`ON`/`OFF`), plus the null-publisher no-op path. Existing
  `test_network` / `test_resolver` stay green and offline (only the running sim
  connects out; unit tests construct components against controlled addresses).
- **Rename:** update the SNTP test and any references to the new `NtpClient`
  name.
- **Build:** sim + OPTA cross-build clean. The `TARGET_OPTA`-confinement guard
  (`grep -rn TARGET_OPTA src --include=*.cpp --include=*.h | grep -v
  'src/libbotanik/hal/'`) stays empty.
- **Size goldens:** `MqttAlarmNotifier` / `AlarmNotifier` / `HealthMonitor` are
  not pinned in `libbotanik_sizes.h`, so the `Publisher*` member should not break
  `test_sizes`; verify after the change and update any golden that legitimately
  moves.
- **Manual sim run:** observe the log sequence NET up -> NTP synced -> MQTT
  connected, confirming the real-socket path end to end.

## Non-goals

- No change to the PLC payload layout or the `net`/`ntp`/`mqtt` config block
  shapes.
- No new alarms.
- No extension of the MQTT client (stays publish-only, QoS-0, keepalive) or the
  SNTP client (stays single-query, server-transmit-time, RTC discipline). Use the
  existing minimal implementations as-is.
- No retry/QoS/Last-Will/round-trip-correction work.
