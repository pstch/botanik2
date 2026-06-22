# Confine `TARGET_OPTA` to the HAL

Date: 2026-06-22
Branch: `confine-target-opta-to-hal`

## Goal

`TARGET_OPTA` must appear **only** under `src/libbotanik/hal/`. Everywhere else,
shared code must reach hardware-specific behavior through the HAL, never by
branching on the target macro.

`TARGET_SIMULATOR` stays allowed anywhere: it guards dev-only scaffolding (the PLC
emulator, the site mock-PLC `fill_output()`, stack painting, the realtime/memlock
helpers, assert-abort, Ctrl+C-quit) that drops out of the portable Arduino path by
design. Only `TARGET_OPTA` - the real-hardware behavior the HAL is meant to
abstract - is being relocated.

## Current state

`grep -rn TARGET_OPTA src` outside `hal/` hits exactly four files:

| File | Use of `TARGET_OPTA` |
|---|---|
| `network/network.cpp` | Mbed `EthernetInterface` bring-up, `UDPSocket` (SNTP) |
| `network/mqtt.cpp` | Mbed `TCPSocket` (hand-rolled MQTT 3.1.1) |
| `network/resolver.cpp` | Mbed async DNS (`gethostbyname_async`) |
| `libbotanik_sizes.h` | per-target footprint table (`#if defined(TARGET_OPTA)`) |

The `hal/` uses (`platform.h`, `opta.h`) are legitimate and stay.

Relevant context:

- The `network/` stack is feature-gated **off** by default
  (`BOTANIK_FEATURE_NETWORK`/`_NTP`/`_MQTT` in `libbotanik_features.h`) and is
  "declared but not yet instantiated". The components are not wired into
  `BotanikFirmware` yet, so the link-ownership rewiring below is greenfield.
- The network `.cpp` files always compile; only their Mbed bodies sit under
  `#ifdef TARGET_OPTA`. `hal/net.cpp` follows the same convention.
- `NetworkHandler` already intends to own the link: it holds the
  `NetworkInterface*`, exposes `is_up()`/`interface()`, and hands the interface to
  `NameResolver` purely so `TimeSyncHandler`/`MqttClient` can reach it. That
  hand-off is the coupling we remove.

## Design

### Part 1 - A minimal HAL socket layer

Two new files, both under `hal/` (so any internal `#ifdef TARGET_OPTA` is legal):

- **`hal/net.h`** - the portable interface. Declarations only; no Mbed or BSD
  socket headers leak out of it. Included by the network components.
- **`hal/net.cpp`** - one `#ifdef TARGET_OPTA` / `#else` split holding both
  backends. All `netsocket/*` (OPTA) and `<sys/socket.h>`/`<netdb.h>` (sim)
  includes are confined here.

Interface (`namespace Botanik { namespace HAL { namespace Net { ... }}}`),
deliberately small and matched to exactly what the three components need:

```cpp
struct Ip4 { uint8_t b[4]; };   // IPv4, network order; mirrors Network::ResolvedAddr

// ---- Link: the single network interface. NetworkHandler drives this. ----
struct LinkConfig {
    bool dhcp;
    char ip[16], netmask[16], gateway[16], dns1[16], dns2[16];   // dotted; "" = unset
};
enum LinkState { LINK_DOWN, LINK_CONNECTING, LINK_UP };
void      link_configure(const LinkConfig&);   // non-blocking (re)bring-up
void      link_disconnect();
LinkState link_status();
bool      link_ip(Ip4* out);                   // current address; false if not up

// ---- DNS: non-blocking, poll model (fits the resolver's existing slots) ----
typedef uint32_t DnsHandle;                    // 0 == invalid
enum DnsResult { DNS_PENDING, DNS_OK, DNS_FAILED };
DnsHandle dns_begin(const char* host);         // 0 if it could not be started
DnsResult dns_poll(DnsHandle, Ip4* out);       // OK/FAILED consume the handle
void      dns_cancel(DnsHandle);

// ---- UDP (NTP) / TCP (MQTT). recv: >0 bytes, 0 would-block, <0 closed/error ----
typedef int32_t UdpSocket;   // <0 invalid
typedef int32_t TcpSocket;   // <0 invalid
UdpSocket udp_open();
bool      udp_sendto(UdpSocket, const Ip4&, uint16_t port, const void* buf, size_t len);
int       udp_recvfrom(UdpSocket, void* buf, size_t len, Ip4* from, uint16_t* port);
void      udp_close(UdpSocket);
TcpSocket tcp_open();
int       tcp_connect(TcpSocket, const Ip4&, uint16_t port);  // 1 done, 0 pending, <0 err
int       tcp_send(TcpSocket, const void* buf, size_t len);   // bytes, <0 err
int       tcp_recv(TcpSocket, void* buf, size_t len);         // bytes, 0 would-block, <0 closed/err
void      tcp_close(TcpSocket);
```

Notes on the surface:

- The interface is a **single implicit link**. Sockets do not take an interface
  handle: the backend opens them on the active link. This is what lets the
  `NetworkInterface*` threading disappear.
- `recv` semantics deliberately distinguish would-block (`0`) from
  closed/error (`<0`), because `MqttClient::_service` treats a peer close as a
  fault and `_poll_connack` needs a short-read distinction.
- The DNS poll model mirrors the resolver's existing `RESOLVE_PENDING/OK/FAILED`
  states, so the OPTA backend internally adapts Mbed's async callback into a
  stored result and the sim does a `getaddrinfo` (literals stay a fast path in
  the resolver, ahead of any HAL call).

**Backends:**

- **OPTA** (`#ifdef TARGET_OPTA`): wraps the lazily-constructed `EthernetInterface`
  and Mbed `UDPSocket`/`TCPSocket`, exactly the calls that live in the network
  `.cpp`s today, just relocated. A small fixed-size socket table maps the integer
  handles to Mbed socket objects.
- **Simulator** (`#else`): real Linux BSD sockets - non-blocking `socket()` +
  `connect`/`send`/`recv`/`sendto`/`recvfrom`, and `getaddrinfo` for DNS. Link is
  always `LINK_UP`; `link_ip` reports the host's primary address (or loopback).
  So NTP and MQTT genuinely work in the simulator against real servers/brokers
  instead of being stubbed dead.

### Part 2 - Rewire the network components (removes every `TARGET_OPTA`)

- **`NameResolver`**: drop `_net`, `set_interface()`, `interface()`. It becomes
  pure name resolution: dotted-decimal literal fast path, otherwise
  `HAL::Net::dns_begin/poll/cancel`. `resolver.h` loses the `NetworkInterface`
  forward declaration.

- **`NetworkHandler`** owns the link. It drives
  `HAL::Net::link_configure/disconnect`, derives its state from
  `HAL::Net::link_status()`, and logs the address from `HAL::Net::link_ip()`. It
  no longer holds a `NetworkInterface*` and no longer wires the resolver. `is_up()`
  stays as the **authoritative link-state accessor other components consult**.

- **`TimeSyncHandler`** / **`MqttClient`**: replace Mbed `UDPSocket`/`TCPSocket`
  with the HAL `udp_*`/`tcp_*` calls. The old "is the link up?" gate
  (`resolver->interface() != nullptr`) becomes `net->is_up()`, where `net` is a
  `const NetworkHandler*` they now hold. They keep the `NameResolver*` for DNS.
  This is the only constructor-signature change; since these components are not
  instantiated yet, there is no existing wiring to update beyond their own
  headers.

Result: the resolver does names, the NetworkHandler owns the link, the HAL owns
the device. No shared component branches on the target.

### Part 3 - `libbotanik_sizes.h`

Flip the discriminator from the forbidden macro to the allowed one:

```c
#if !defined(TARGET_SIMULATOR)   // was: #if defined(TARGET_OPTA)
#  define BOTANIK_SIZE(host_, opta_) (opta_)
#else
#  define BOTANIK_SIZE(host_, opta_) (host_)
#endif
```

Behavior is identical (the non-sim build is the OPTA-class build), now keyed off
`TARGET_SIMULATOR`. One-line change.

## Non-goals

- No HAL abstraction for the dev-only `TARGET_SIMULATOR` scaffolding; it stays as
  is, per the rule.
- No change to the network feature flags or to when the stack is instantiated.
- No new wire-protocol behavior; MQTT/SNTP logic is unchanged, only its transport
  calls move behind the HAL.

## Verification

1. `make -C build` (SIMULATOR) builds clean; `botanik_sim` runs.
2. Cross-build OPTA (`cmake -B build-opta -DBOTANIK_TARGET=OPTA && make -C build-opta`)
   builds clean.
3. The guard holds:
   ```sh
   grep -rn TARGET_OPTA src --include=*.cpp --include=*.h \
     | grep -v 'src/libbotanik/hal/'
   ```
   returns nothing.
4. `libbotanik_sizes.h` static-asserts still pass on both targets (no footprint
   regression).
5. (Optional, sim) point NTP/MQTT config at a real server/broker and confirm the
   simulator now syncs / connects via the BSD-socket backend.
