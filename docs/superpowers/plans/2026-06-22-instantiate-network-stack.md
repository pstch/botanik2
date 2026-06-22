# Instantiate the network / NTP / MQTT stack - Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fully activate the gh_mlb41 network stack - consolidate the alarm notifier behind a `Publisher` seam, wire the live link gate, and rename the SNTP client to `NtpClient` - so the firmware brings up the link, syncs the RTC over SNTP, connects to the broker, and publishes alarm transitions over MQTT.

**Architecture:** The feature flags (`BOTANIK_FEATURE_NETWORK/_NTP/_MQTT`) and the simulator's mock-PLC config seeding (DHCP, `pool.ntp.org`, `test.mosquitto.org:1883`) are ALREADY in the tree and compile. The remaining work closes three gaps: (1) the alarm notifier is a stub with no transport; (2) the SNTP/MQTT clients are constructed with a null `NetworkHandler*`, so their link gate is always closed and they stay dormant; (3) `TimeSyncHandler` should be renamed `NtpClient`.

**Tech Stack:** C++11, CMake, doctest (host-only), Mbed OS (OPTA), POSIX BSD sockets (simulator), `HAL::Net` socket layer.

## Current state (verified in the worktree)

- `sites/gh_mlb41/config.h` already `#define`s `BOTANIK_FEATURE_NETWORK/_NTP/_MQTT = 1` (with a stale comment claiming the sim TCP/IP is "inert").
- `src/botanik_sim/main.cpp` already seeds `shared_out.conf` (net DHCP, ntp `pool.ntp.org`, mqtt `test.mosquitto.org:1883`), serials = 1.
- `BotanikFirmware` already declares the network members, runs their `step()` in `loop()`, and exposes accessors - all gated on.
- `botanik` and `botanik_sim` build today; the stack is dormant because the firmware passes 3 ctor args to the SNTP/MQTT clients (the 4th `const NetworkHandler*` link gate defaults to `nullptr`, so the gate `_net == nullptr || !_net->is_up()` never opens), and because `MqttAlarmNotifier`'s transport is a no-op stub.

## Global Constraints

- `TARGET_OPTA` may appear ONLY under `src/libbotanik/hal/`. This work adds none; the guard `grep -rn TARGET_OPTA src --include=*.cpp --include=*.h | grep -v 'src/libbotanik/hal/'` must stay empty.
- C++11 only: no `if constexpr`, no `<initializer_list>`, no digit separators, no `std::atomic`.
- No em-dashes in comments/docs; use ' - ', a colon, or parens. Terse comments.
- Member variables at the top of a class, methods after; Doxygen `@name` member-groups per scope.
- The MQTT topic prefix is `Config::MQTT_TOPIC_PREFIX` (gh_mlb41: `"botanik/greenhouse"`); the alarm topic format is `"<prefix>/alarms/<CODE>"`, payload `"ON"`/`"OFF"`.
- Unit tests are host-only and must stay deterministic offline (they construct components directly; only the running `botanik_sim` makes outbound connections).
- After every task the tree must build for BOTH targets and the host doctest suite must pass.
- Commit messages: short, casual, no `Co-Authored-By` trailer.

**Build / test commands** (from the worktree root). `make check` runs clang-tidy which may fail for pre-existing reasons unrelated to this work, so build the test binary and run it directly:

```bash
# SIMULATOR: build the library, tests, and sim binary; run the doctest suite directly
cmake -B build -DBOTANIK_TARGET=SIMULATOR >/dev/null && make -C build botanik_tests botanik_sim -j$(nproc)
./build/tests/botanik_tests            # expect "NNN passed | 0 failed"

# OPTA cross-build
cmake -B build-opta -DBOTANIK_TARGET=OPTA >/dev/null && make -C build-opta -j$(nproc)

# Guard (must print nothing)
grep -rn TARGET_OPTA src --include=*.cpp --include=*.h | grep -v 'src/libbotanik/hal/'
```

---

### Task 1: Consolidate the alarm notifier behind a `Publisher` seam

Replace the abstract `AlarmNotifier` + concrete stub `MqttAlarmNotifier` with a single concrete `AlarmNotifier` that forwards to an abstract `Publisher`. `MqttClient` implements `Publisher`. `HealthMonitor` gains `set_alarm_publisher()`.

**Files:**
- Create: `src/libbotanik/alarms/publisher.h`
- Modify: `src/libbotanik/alarms/alarms.h` (replace the abstract `AlarmNotifier` with a concrete one; add includes)
- Delete: `src/libbotanik/alarms/notifier.h`
- Modify: `src/libbotanik/health/health.h` (member `_mqtt` -> `_notifier`; `set_alarm_publisher`; includes)
- Modify: `src/libbotanik/network/mqtt.h` (inherit `Alarms::Publisher`; add 2-arg `publish` override; drop the default arg)
- Test: `tests/test_notifier.cpp` (new)
- Modify: `tests/CMakeLists.txt` (register the new test source)

**Interfaces:**
- Consumes: `Alarms::AlarmCondition` (has `const char* code;` first field), `Config::MQTT_TOPIC_PREFIX`, `MqttClient::publish(const char*, const char*, bool)`.
- Produces:
  - `struct Botanik::Alarms::Publisher { virtual ~Publisher(); virtual bool publish(const char* topic, const char* payload) = 0; };`
  - concrete `Botanik::Alarms::AlarmNotifier`: `explicit AlarmNotifier(const char* prefix); void set_publisher(Publisher*); void notify_on(const AlarmCondition&); void notify_off(const AlarmCondition&);`
  - `HealthMonitor::set_alarm_publisher(Alarms::Publisher*)`
  - `MqttClient : public ConfigHandler, public Alarms::Publisher` with `bool publish(const char* topic, const char* payload) override`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_notifier.cpp`:

```cpp
/**
 * @file test_notifier.cpp
 * @brief AlarmNotifier formats "<prefix>/alarms/<CODE>" + ON/OFF and forwards to a
 *        Publisher; with no publisher it is a no-op.
 */
#include <cstring>
#include <string>

#include "doctest.h"

#include "libbotanik/alarms/alarms.h"
#include "libbotanik/alarms/publisher.h"

using namespace Botanik;
using namespace Botanik::Alarms;

namespace {
    struct MockPublisher : Publisher {
        int  calls = 0;
        char topic[128] = {0};
        char payload[16] = {0};
        bool publish(const char* t, const char* p) override {
            calls++;
            std::strncpy(topic, t, sizeof(topic) - 1);
            std::strncpy(payload, p, sizeof(payload) - 1);
            return true;
        }
    };
}

TEST_CASE("AlarmNotifier: notify_on/off build the topic + payload and forward to the publisher") {
    MockPublisher pub;
    AlarmNotifier n("botanik/greenhouse");
    n.set_publisher(&pub);

    AlarmCondition c = {};
    c.code = "NET_FLT";

    n.notify_on(c);
    CHECK(pub.calls == 1);
    CHECK(std::string(pub.topic)   == "botanik/greenhouse/alarms/NET_FLT");
    CHECK(std::string(pub.payload) == "ON");

    n.notify_off(c);
    CHECK(pub.calls == 2);
    CHECK(std::string(pub.payload) == "OFF");
}

TEST_CASE("AlarmNotifier: with no publisher set it is a safe no-op") {
    AlarmNotifier n("botanik/greenhouse");
    AlarmCondition c = {};
    c.code = "NET_FLT";
    n.notify_on(c);    // must not crash / dereference null
    n.notify_off(c);
    CHECK(true);
}
```

- [ ] **Step 2: Register the test and run it (red)**

In `tests/CMakeLists.txt`, add `test_notifier.cpp` to the `add_executable(botanik_tests ...)` source list (next to `test_network.cpp`).

Run: `cmake -B build -DBOTANIK_TARGET=SIMULATOR >/dev/null && make -C build botanik_tests 2>&1 | tail -15`
Expected: FAIL - `libbotanik/alarms/publisher.h` not found / `AlarmNotifier` has no `set_publisher`.

- [ ] **Step 3: Create `alarms/publisher.h`**

```cpp
#ifndef ALARMS_PUBLISHER_H
#define ALARMS_PUBLISHER_H

/**
 * @file publisher.h
 * @brief Publisher - the transport seam an AlarmNotifier publishes through.
 *
 * Decouples alarm notification (alarms/) from the network stack: AlarmNotifier
 * formats a topic/payload and hands it to a Publisher; MqttClient implements
 * Publisher. alarms/ depends only on this interface, never on network/.
 */

namespace Botanik { namespace Alarms {

    /** @brief Abstract publish sink (e.g. an MQTT client). */
    struct Publisher {
        virtual ~Publisher() {}
        /** @brief Publish @p payload under @p topic. Returns false if it could not
         *  be sent (e.g. not connected); the caller may drop or retry. */
        virtual bool publish(const char* topic, const char* payload) = 0;
    };

}}

#endif // ALARMS_PUBLISHER_H
```

- [ ] **Step 4: Make `AlarmNotifier` concrete in `alarms/alarms.h`**

Add includes near the top of `alarms.h` (it already includes `<cstring>`; add `<cstdio>` for `snprintf` and the publisher header):

```cpp
#include <cstdio>                              // snprintf (AlarmNotifier::_emit)
#include "libbotanik/alarms/publisher.h"       // Publisher
```

Replace the abstract `AlarmNotifier` (the `class AlarmNotifier { ... notify_off(...) = 0; };` block) with this concrete class:

```cpp
    /** @brief Formats an alarm transition into "<prefix>/alarms/<CODE>" + "ON"/"OFF"
     *  and forwards it to a Publisher. The model calls notify_on/off; the publisher
     *  is wired later (set_publisher), so it is a no-op until then. */
    class AlarmNotifier {
    private:
        /** @name State
         *  @{ */
        const char* _prefix;   // topic root, e.g. "botanik/greenhouse"
        Publisher*  _pub;       // transport (null = no-op)
        /** @} */

    public:
        /** @brief @p prefix is the MQTT topic root. */
        explicit AlarmNotifier(const char* prefix)
            : _prefix(prefix && prefix[0] ? prefix : "botanik"), _pub(nullptr) {}

        /** @brief Wire the transport (called once at firmware setup). */
        void set_publisher(Publisher* pub) { _pub = pub; }

        void notify_on(const AlarmCondition& a)  { _emit(a, true); }
        void notify_off(const AlarmCondition& a) { _emit(a, false); }

    private:
        /** @brief Build "<prefix>/alarms/<CODE>" + ON/OFF and publish (no-op if unwired). */
        void _emit(const AlarmCondition& a, bool on) {
            if (_pub == nullptr) return;
            char topic[64];
            snprintf(topic, sizeof(topic), "%s/alarms/%s", _prefix, a.code ? a.code : "?");
            _pub->publish(topic, on ? "ON" : "OFF");
        }
    };
```

(`AlarmNotifier` is defined ahead of `AlarmModel`, which holds an `AlarmNotifier*` and calls `notify_on/off` inline - keep that order. The calls are now non-virtual; `AlarmModel` already stores the exact `AlarmNotifier*`.)

- [ ] **Step 5: Delete `alarms/notifier.h` and update `health.h`**

Delete `src/libbotanik/alarms/notifier.h`.

In `src/libbotanik/health/health.h`:
- Replace `#include "libbotanik/alarms/notifier.h"` with `#include "libbotanik/alarms/alarms.h"` (if `alarms.h` is not already included transitively, this makes `AlarmNotifier`/`Publisher` visible).
- Change the member `Alarms::MqttAlarmNotifier _mqtt;` to `Alarms::AlarmNotifier _notifier;`.
- In the ctor initializer list, change `_mqtt(Config::MQTT_TOPIC_PREFIX)` to `_notifier(Config::MQTT_TOPIC_PREFIX)`, and the model init `_model(table, count, logger, &_mqtt, ...)` to `_model(table, count, logger, &_notifier, ...)`.
- Add a public method (near `model()`):

```cpp
        /** @brief Wire the alarm notifier's transport (the firmware passes the MQTT
         *  client once it exists). Until then the notifier is a no-op. */
        void set_alarm_publisher(Alarms::Publisher* pub) { _notifier.set_publisher(pub); }
```

- [ ] **Step 6: Make `MqttClient` implement `Alarms::Publisher`**

In `src/libbotanik/network/mqtt.h`:
- Add `#include "libbotanik/alarms/publisher.h"`.
- Change the class header `class MqttClient : public ConfigHandler {` to `class MqttClient : public ConfigHandler, public Alarms::Publisher {`.
- Change the existing publish declaration `bool publish(const char* topic, const char* payload, bool retain = false);` to drop the default: `bool publish(const char* topic, const char* payload, bool retain);`.
- Add the 2-arg override in the public section:

```cpp
        /** @brief Alarms::Publisher: publish non-retained (QoS 0). */
        bool publish(const char* topic, const char* payload) override {
            return publish(topic, payload, false);
        }
```

No change to `mqtt.cpp` is required (the 3-arg `publish` definition is unchanged; callers passing two args now bind to the override).

- [ ] **Step 7: Build + run tests (green)**

Run: `make -C build botanik_tests 2>&1 | tail -8 && ./build/tests/botanik_tests 2>&1 | tail -3`
Expected: build succeeds; the two `AlarmNotifier` cases pass; the suite is green (no `MqttAlarmNotifier` references remain).

- [ ] **Step 8: OPTA cross-build**

Run: `cmake -B build-opta -DBOTANIK_TARGET=OPTA >/dev/null && make -C build-opta -j$(nproc) 2>&1 | tail -8`
Expected: clean.

- [ ] **Step 9: Commit**

```bash
git add src/libbotanik/alarms/publisher.h src/libbotanik/alarms/alarms.h \
        src/libbotanik/health/health.h src/libbotanik/network/mqtt.h \
        tests/test_notifier.cpp tests/CMakeLists.txt
git rm src/libbotanik/alarms/notifier.h
git commit -m "alarms: one AlarmNotifier behind a Publisher seam; MqttClient implements it"
```

---

### Task 2: Rename `TimeSyncHandler` -> `NtpClient`

Mechanical rename across the network module, the firmware member/accessor, the test, and docs. No behavior change.

**Files:**
- Modify: `src/libbotanik/network/network.h`
- Modify: `src/libbotanik/network/network.cpp`
- Modify: `src/libbotanik/firmware.h` (member `_time_sync` -> `_ntp`, accessor `time_sync()` -> `ntp()`)
- Modify: `tests/test_network.cpp`
- Modify: `docs/PLC_CONFIG.md`, `docs/library.dox`

**Interfaces:**
- Produces: `Botanik::Network::NtpClient` (same ctor signature as `TimeSyncHandler`: `(PlcIO::PlcSyncContext*, Logs::Logger*, NameResolver*, const NetworkHandler*)`), `BotanikFirmware::ntp()` returning `Network::NtpClient&`.

- [ ] **Step 1: Rename the class in `network.h`**

Replace every occurrence of the token `TimeSyncHandler` with `NtpClient` in `src/libbotanik/network/network.h` (the class name, the constructor name, and any doc text). Update the class doc comment to read "SNTP client" / "NtpClient" naturally.

- [ ] **Step 2: Rename in `network.cpp`**

Replace every `TimeSyncHandler` with `NtpClient` in `src/libbotanik/network/network.cpp` (all `TimeSyncHandler::` method-definition prefixes, the ctor, and comments).

- [ ] **Step 3: Rename the firmware member + accessor in `firmware.h`**

In `src/libbotanik/firmware.h`:
- Member: `Network::TimeSyncHandler  _time_sync;` -> `Network::NtpClient  _ntp;`
- Ctor init: `, _time_sync(_plc_handler.context(), &_logger, &_resolver)` -> `, _ntp(_plc_handler.context(), &_logger, &_resolver)` (the 4th arg is added in Task 3; leave it 3-arg here so this task stays a pure rename and still builds).
- Loop: `_time_sync.step(now_ms);` -> `_ntp.step(now_ms);`
- Accessor: `Network::TimeSyncHandler&  time_sync()  { return _time_sync; }` -> `Network::NtpClient&  ntp()  { return _ntp; }` (update its doc comment too).

- [ ] **Step 4: Rename in the test**

In `tests/test_network.cpp`, replace `TimeSyncHandler` with `NtpClient` (the include is unchanged - same header; only the type name changes). Update the `TEST_CASE` names/comments that say "TimeSyncHandler" to "NtpClient".

- [ ] **Step 5: Rename in docs**

In `docs/PLC_CONFIG.md` and `docs/library.dox`, replace `TimeSyncHandler` references with `NtpClient`. (Grep first: `grep -rn TimeSyncHandler docs/` - update each hit. Comments in `libbotanik_config.h` / `libbotanik_features.h` / `plcio/config.h` / `config_handler.h` / `hal/opta.h` that mention `TimeSyncHandler` should also be updated for consistency - `grep -rn TimeSyncHandler src/` and fix the comment occurrences; there is no remaining code token after Steps 1-4.)

- [ ] **Step 6: Verify no `TimeSyncHandler` token remains**

Run: `grep -rn TimeSyncHandler src/ tests/ docs/`
Expected: no output (except this plan file and the spec, which are historical and may keep the old name in prose - do not edit them).

- [ ] **Step 7: Build + run tests (green)**

Run: `make -C build botanik_tests botanik_sim 2>&1 | tail -6 && ./build/tests/botanik_tests 2>&1 | tail -3`
Expected: build succeeds; suite green.

- [ ] **Step 8: OPTA cross-build + commit**

Run: `make -C build-opta -j$(nproc) 2>&1 | tail -6`
Expected: clean.

```bash
git add src/libbotanik/network/network.h src/libbotanik/network/network.cpp \
        src/libbotanik/firmware.h tests/test_network.cpp docs/PLC_CONFIG.md docs/library.dox \
        src/libbotanik_config.h src/libbotanik_features.h src/libbotanik/plcio/config.h \
        src/libbotanik/config_handler.h src/libbotanik/hal/opta.h
git commit -m "network: rename TimeSyncHandler -> NtpClient"
```

(Only `git add` the files you actually changed; drop any from the list above that had no `TimeSyncHandler` reference.)

---

### Task 3: Activate the live wiring + fix the stale comment + verify end to end

Pass the `NetworkHandler` link gate to the SNTP/MQTT clients (so they leave the dormant null-gate state) and wire the alarm publisher. Fix the stale sim comment. Verify the whole stack.

**Files:**
- Modify: `src/libbotanik/firmware.h` (ctor 4th arg; `setup()` publisher wiring)
- Modify: `src/sites/gh_mlb41/config.h` (stale comment)

**Interfaces:**
- Consumes: `HealthMonitor::set_alarm_publisher(Alarms::Publisher*)` (Task 1), `Network::NtpClient` / `_ntp` (Task 2), the existing `_network` member and `MqttClient _mqtt` (which is now an `Alarms::Publisher`).

- [ ] **Step 1: Pass the link gate to both clients (`firmware.h` ctor)**

In the `BotanikFirmware` ctor initializer list, change the two client constructions to pass `&_network` as the 4th arg:

```cpp
#if BOTANIK_FEATURE_NTP
              , _ntp(_plc_handler.context(), &_logger, &_resolver, &_network)
#endif
#if BOTANIK_FEATURE_MQTT
              , _mqtt(_plc_handler.context(), &_logger, &_resolver, &_network)
#endif
```

(`_network` is declared before `_ntp`/`_mqtt`, so it is fully constructed first - member-init order follows declaration order.)

- [ ] **Step 2: Wire the alarm publisher in `setup()`**

In `BotanikFirmware::setup()`, just before the `on_setup();` call, add:

```cpp
#if BOTANIK_FEATURE_MQTT
            // Route alarm transitions out through the MQTT client (it is an
            // Alarms::Publisher). Until now the notifier was a no-op.
            _health.set_alarm_publisher(&_mqtt);
#endif
```

- [ ] **Step 3: Fix the stale comment in `sites/gh_mlb41/config.h`**

Replace the networking comment block (the one saying "on the simulator the TCP/IP stack is inert (no sockets), so they stay idle") with:

```c
// Networking: this greenhouse's PLC coprocessor publishes the net / NTP / MQTT
// config blocks, so enable the network stack, SNTP time sync, and the MQTT alarm
// notifier. Functional on both targets: OPTA over Mbed sockets, and the simulator
// over real host BSD sockets (HAL::Net), so the sim genuinely brings up the link,
// syncs time, and connects to the broker.
```

- [ ] **Step 4: Build everything + run the doctest suite**

Run: `make -C build botanik_tests botanik_sim -j$(nproc) 2>&1 | tail -8 && ./build/tests/botanik_tests 2>&1 | tail -3`
Expected: build succeeds; suite green (all prior tests + the two notifier tests).

- [ ] **Step 5: OPTA cross-build**

Run: `make -C build-opta -j$(nproc) 2>&1 | tail -8`
Expected: clean.

- [ ] **Step 6: Guard + size goldens**

Run: `grep -rn TARGET_OPTA src --include=*.cpp --include=*.h | grep -v 'src/libbotanik/hal/'`
Expected: empty.

The size self-check runs inside the doctest suite (`test_sizes`). If a `static_assert` in `src/libbotanik_sizes.h` fails after the notifier/`MqttClient` change, the failure prints the expected vs actual byte size; update the affected `BOTANIK_SIZE(host_, opta_)` entry to the reported actual values (host from this build, opta from the cross-build's static-assert message) and re-run. If the suite already passed in Step 4, no size entry moved - skip this.

- [ ] **Step 7: Best-effort live sim check**

Run the sim briefly and look for the link/NTP/MQTT log lines on the MONITOR output. The sim is an interactive TUI, so capture a short headless run:

```bash
timeout 15 ./build/botanik_sim </dev/null 2>&1 | grep -iE "network up|NTP synced|MQTT connected|network bring-up" | head
```

Expected (with outbound internet available): lines for "network up", and ideally "NTP synced" / "MQTT connected". If the environment has no outbound network, you will at least see "network bring-up"/"network up" (the sim link is always up); NTP/MQTT may stay in retry. Note in the report whether live NTP/MQTT was observed or the environment blocked it - the build + unit tests are the hard gate; the live connect is best-effort.

- [ ] **Step 8: Commit**

```bash
git add src/libbotanik/firmware.h src/sites/gh_mlb41/config.h
# include src/libbotanik_sizes.h only if Step 6 updated a golden
git commit -m "firmware: wire the live link gate + MQTT alarm publisher"
```

---

## Self-Review

- **Spec coverage:**
  - Enable flags -> already in `gh_mlb41/config.h` (verified); the stale comment is fixed in Task 3. Covered.
  - Fix stale ctor wiring (4th `&_network` arg) -> Task 3 Step 1. Covered.
  - Consolidate notifier (one `AlarmNotifier` + `Publisher`; `MqttClient` implements it; `HealthMonitor::set_alarm_publisher`; base `setup()` wires it) -> Task 1 (interface + impl) + Task 3 Step 2 (wiring). Covered.
  - Rename `TimeSyncHandler` -> `NtpClient` -> Task 2. Covered.
  - Sim mock PLC publishes config -> already in `botanik_sim/main.cpp` (verified). Covered (no task needed).
  - Tests (notifier + mock publisher; existing green) -> Task 1 Steps 1-2, 7. Covered.
  - Build sim + OPTA, guard clean, size goldens, manual sim run -> Task 3 Steps 4-7. Covered.
- **Placeholder scan:** No TBD/TODO. The size-golden step (Task 3 Step 6) is conditional but concrete (it names the exact macro to update and the source of the values). The rename `git add` list is qualified ("only files you changed").
- **Type consistency:** `Alarms::Publisher::publish(const char*, const char*)` (bool) is used consistently; `AlarmNotifier(const char*)` / `set_publisher(Publisher*)` / `notify_on/off(const AlarmCondition&)`; `HealthMonitor::set_alarm_publisher(Alarms::Publisher*)`; `Network::NtpClient` and `BotanikFirmware::ntp()`/`_ntp`; the 4-arg client ctor `(ctx, logger, resolver, net)` matches the refactor's signatures.
