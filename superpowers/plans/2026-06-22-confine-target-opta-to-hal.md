# Confine `TARGET_OPTA` to the HAL - Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move every use of `TARGET_OPTA` out of shared code and behind a small HAL socket layer, so the macro appears only under `src/libbotanik/hal/`.

**Architecture:** A new `hal/net.h` declares a minimal link/DNS/UDP/TCP interface; `hal/net.cpp` holds both backends behind one `#ifdef TARGET_OPTA / #else` (Mbed sockets vs Linux BSD sockets). The network components (`NameResolver`, `NetworkHandler`, `TimeSyncHandler`, `MqttClient`) are rewired onto that interface, dropping the Mbed types and the `NetworkInterface*` threading. `libbotanik_sizes.h` switches its per-target table to key off `TARGET_SIMULATOR`.

**Tech Stack:** C++11, CMake, doctest (host-only unit tests), Mbed OS (OPTA), POSIX BSD sockets (simulator).

## Global Constraints

- `TARGET_OPTA` may appear ONLY under `src/libbotanik/hal/`. Nowhere else.
- `TARGET_SIMULATOR` is allowed anywhere (it guards dev-only scaffolding).
- C++11 only: no `if constexpr`, no `<initializer_list>`, no digit separators, no `std::atomic` (use `volatile` + `HAL::dsb()`/`ordered_read`).
- No em-dashes in comments/docs/prose; use ' - ', a colon, or parens.
- Comments terse; even terser in simulator code.
- Member variables at the top of a class, methods after; Doxygen `@name` member-groups per scope.
- Unit tests are host-only (SIMULATOR). They must stay deterministic offline: use RFC 6761 `.invalid` names (guaranteed non-resolvable) and RFC 5737 TEST-NET-1 addresses (`192.0.2.0/24`, non-routable) so DNS/connect outcomes do not depend on real network.
- After every task the tree must build for BOTH targets (SIMULATOR and OPTA cross-build) and the host test suite must pass.
- Commit messages: short, casual, no `Co-Authored-By` trailer.

**Build / test commands** (run from the worktree root):

```bash
# SIMULATOR build + host tests
cmake -B build -DBOTANIK_TARGET=SIMULATOR >/dev/null && make -C build -j$(nproc)
make -C build check           # builds + runs the doctest suite

# OPTA cross-build (requires arduino:mbed_opta core installed)
cmake -B build-opta -DBOTANIK_TARGET=OPTA >/dev/null && make -C build-opta -j$(nproc)

# The guard: must print nothing
grep -rn TARGET_OPTA src --include=*.cpp --include=*.h | grep -v 'src/libbotanik/hal/'
```

> Note: if the OPTA toolchain is unavailable in this environment, run the SIMULATOR build + `make check` after every task, and run the OPTA cross-build at least once at the end (Task 6). Flag explicitly if OPTA could not be verified.

---

### Task 1: Flip `libbotanik_sizes.h` to key off `TARGET_SIMULATOR`

Smallest, independent change. Behavior-identical (the non-sim build is the OPTA-class build), but removes a `TARGET_OPTA` use from a non-hal file.

**Files:**
- Modify: `src/libbotanik_sizes.h:27-31`

**Interfaces:**
- Consumes: nothing.
- Produces: nothing (the `BOTANIK_SIZE(host_, opta_)` macro keeps the same name and semantics).

- [ ] **Step 1: Make the change**

Replace the discriminator block. Current (lines 27-31):

```c
#if defined(TARGET_OPTA)
#  define BOTANIK_SIZE(host_, opta_) (opta_)
#else
#  define BOTANIK_SIZE(host_, opta_) (host_)
#endif
```

New:

```c
// Pick the per-target footprint. Keyed off TARGET_SIMULATOR (not TARGET_OPTA): the
// macro is confined to the HAL, and the non-sim build is the OPTA-class build.
#if !defined(TARGET_SIMULATOR)
#  define BOTANIK_SIZE(host_, opta_) (opta_)
#else
#  define BOTANIK_SIZE(host_, opta_) (host_)
#endif
```

- [ ] **Step 2: Build SIMULATOR + run the size assertions**

Run: `cmake -B build -DBOTANIK_TARGET=SIMULATOR >/dev/null && make -C build check 2>&1 | tail -20`
Expected: build succeeds; `test_sizes` passes (the host branch is selected exactly as before).

- [ ] **Step 3: Confirm this file is now clean**

Run: `grep -n TARGET_OPTA src/libbotanik_sizes.h`
Expected: no output.

- [ ] **Step 4: Commit**

```bash
git add src/libbotanik_sizes.h
git commit -m "sizes: key the footprint table off TARGET_SIMULATOR"
```

---

### Task 2: Add the HAL socket layer (`hal/net.h` + `hal/net.cpp`)

Foundational. New files only - the existing network code is untouched, so the tree keeps building. Both backends live here; the simulator backend uses real BSD sockets.

**Files:**
- Create: `src/libbotanik/hal/net.h`
- Create: `src/libbotanik/hal/net.cpp`
- Test: `tests/test_hal_net.cpp`
- Modify: `tests/CMakeLists.txt` (register the new test source - see Step 6)

**Interfaces:**
- Consumes: nothing.
- Produces: `namespace Botanik::HAL::Net` with:
  - `struct Ip4 { uint8_t b[4]; };`
  - `struct LinkConfig { bool dhcp; char ip[16], netmask[16], gateway[16], dns1[16], dns2[16]; };`
  - `enum LinkState { LINK_DOWN, LINK_CONNECTING, LINK_UP };`
  - `void link_configure(const LinkConfig&);` `void link_disconnect();` `LinkState link_status();` `bool link_ip(Ip4* out);`
  - `typedef uint32_t DnsHandle;` `enum DnsResult { DNS_PENDING, DNS_OK, DNS_FAILED };`
  - `DnsHandle dns_begin(const char* host);` `DnsResult dns_poll(DnsHandle, Ip4* out);` `void dns_cancel(DnsHandle);`
  - `typedef int32_t UdpSocket; typedef int32_t TcpSocket;`
  - `UdpSocket udp_open();` `bool udp_sendto(UdpSocket, const Ip4&, uint16_t, const void*, size_t);` `int udp_recvfrom(UdpSocket, void*, size_t, Ip4*, uint16_t*);` `void udp_close(UdpSocket);`
  - `TcpSocket tcp_open();` `int tcp_connect(TcpSocket, const Ip4&, uint16_t);` `int tcp_send(TcpSocket, const void*, size_t);` `int tcp_recv(TcpSocket, void*, size_t);` `void tcp_close(TcpSocket);`
  - recv convention: `>0` bytes, `0` would-block, `<0` closed/error. `tcp_connect`: `1` connected, `0` in-progress, `<0` error. Invalid socket/handle: `<0` / `0`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_hal_net.cpp`:

```cpp
/**
 * @file test_hal_net.cpp
 * @brief Host-side smoke tests for the HAL::Net simulator backend (real BSD sockets).
 *        Kept offline-deterministic: DNS uses a guaranteed-unresolvable .invalid name,
 *        TCP connects to a non-routable TEST-NET-1 address.
 */
#include <cstdint>
#include <cstring>

#include "doctest.h"

#include "libbotanik/hal/net.h"

using namespace Botanik::HAL;

TEST_CASE("HAL::Net sim: the link is up immediately") {
    CHECK(Net::link_status() == Net::LINK_UP);
    Net::Ip4 ip;
    CHECK(Net::link_ip(&ip));   // some address (host primary or loopback)
}

TEST_CASE("HAL::Net sim: DNS of a guaranteed-bad name fails, not pending forever") {
    Net::DnsHandle h = Net::dns_begin("nonexistent.invalid");
    REQUIRE(h != 0);
    Net::Ip4 out;
    // The sim resolves synchronously, so the first poll is terminal (never PENDING).
    Net::DnsResult r = Net::dns_poll(h, &out);
    CHECK(r == Net::DNS_FAILED);
}

TEST_CASE("HAL::Net sim: UDP socket opens and closes") {
    Net::UdpSocket s = Net::udp_open();
    REQUIRE(s >= 0);
    Net::Ip4 dst = {{192, 0, 2, 1}};            // TEST-NET-1, non-routable
    uint8_t pkt[4] = {0};
    CHECK(Net::udp_sendto(s, dst, 123, pkt, sizeof(pkt)));   // local send succeeds
    uint8_t rx[8]; Net::Ip4 from; uint16_t port = 0;
    CHECK(Net::udp_recvfrom(s, rx, sizeof(rx), &from, &port) == 0);   // would-block: no reply
    Net::udp_close(s);
}

TEST_CASE("HAL::Net sim: TCP connect to a dead address never reports connected") {
    Net::TcpSocket s = Net::tcp_open();
    REQUIRE(s >= 0);
    Net::Ip4 dst = {{192, 0, 2, 1}};            // TEST-NET-1: connect can never complete
    int r = Net::tcp_connect(s, dst, 1883);
    CHECK(r <= 0);                               // 0 in-progress or <0 immediate refuse
    Net::tcp_close(s);
}
```

- [ ] **Step 2: Run the test to verify it fails to compile**

Run: `make -C build check 2>&1 | tail -20`
Expected: FAIL - `libbotanik/hal/net.h` not found / `Net::` undeclared.

- [ ] **Step 3: Write `hal/net.h`**

Create `src/libbotanik/hal/net.h`:

```cpp
#ifndef HAL_NET_H
#define HAL_NET_H

/**
 * @file net.h
 * @brief Minimal HAL socket layer: a single network link, non-blocking DNS, UDP and TCP.
 *
 * Portable surface used by the network components (resolver / NTP / MQTT). The two
 * backends live in net.cpp: Mbed sockets on OPTA, Linux BSD sockets on the simulator.
 * No Mbed or POSIX socket headers leak through this header. The link is implicit: a
 * socket is opened on the active interface, so callers never hold an interface handle.
 */

#include <cstddef>
#include <cstdint>

namespace Botanik { namespace HAL { namespace Net {

    /** @name Address */
    /** @{ */
    /** @brief IPv4 address as four octets (network order). */
    struct Ip4 { uint8_t b[4]; };
    /** @} */

    /** @name Link (the single network interface) */
    /** @{ */
    /** @brief Bring-up parameters. Dotted-decimal strings; an empty string means unset.
     *  When dhcp is true the static fields are ignored. */
    struct LinkConfig {
        bool dhcp;
        char ip[16], netmask[16], gateway[16], dns1[16], dns2[16];
    };
    enum LinkState { LINK_DOWN, LINK_CONNECTING, LINK_UP };

    /** @brief Start a non-blocking (re)bring-up from @p cfg. */
    void      link_configure(const LinkConfig& cfg);
    /** @brief Tear the link down (idempotent). */
    void      link_disconnect();
    /** @brief Current link state. */
    LinkState link_status();
    /** @brief Current address into @p out; false if the link is not up. */
    bool      link_ip(Ip4* out);
    /** @} */

    /** @name DNS (non-blocking, poll model) */
    /** @{ */
    typedef uint32_t DnsHandle;        ///< 0 == invalid / could-not-start
    enum DnsResult { DNS_PENDING, DNS_OK, DNS_FAILED };

    /** @brief Start resolving @p host. Returns 0 if a lookup could not be started. */
    DnsHandle dns_begin(const char* host);
    /** @brief Poll @p h. On DNS_OK fills @p out; DNS_OK/DNS_FAILED consume the handle. */
    DnsResult dns_poll(DnsHandle h, Ip4* out);
    /** @brief Cancel an in-flight lookup (idempotent). */
    void      dns_cancel(DnsHandle h);
    /** @} */

    /** @name UDP / TCP
     *  recv returns >0 bytes, 0 would-block, <0 closed/error. A socket id < 0 is invalid. */
    /** @{ */
    typedef int32_t UdpSocket;
    typedef int32_t TcpSocket;

    UdpSocket udp_open();
    bool      udp_sendto(UdpSocket s, const Ip4& addr, uint16_t port, const void* buf, size_t len);
    int       udp_recvfrom(UdpSocket s, void* buf, size_t len, Ip4* from, uint16_t* port);
    void      udp_close(UdpSocket s);

    TcpSocket tcp_open();
    /** @brief Non-blocking connect: 1 connected, 0 in-progress (poll again), <0 error. */
    int       tcp_connect(TcpSocket s, const Ip4& addr, uint16_t port);
    int       tcp_send(TcpSocket s, const void* buf, size_t len);   ///< bytes, <0 error
    int       tcp_recv(TcpSocket s, void* buf, size_t len);         ///< >0 / 0 wouldblock / <0 closed
    void      tcp_close(TcpSocket s);
    /** @} */

}}}

#endif // HAL_NET_H
```

- [ ] **Step 4: Write the simulator backend of `hal/net.cpp`**

Create `src/libbotanik/hal/net.cpp`. Start with the file shell and the `#else` (simulator) backend. The OPTA backend goes in Step 5.

```cpp
/**
 * @file net.cpp
 * @brief HAL::Net backends. OPTA wraps Mbed sockets; the simulator uses Linux BSD
 *        sockets so NTP/MQTT genuinely run on the host. The TARGET_OPTA / TARGET_SIMULATOR
 *        split is legal here: this file is part of the HAL.
 */

#include "libbotanik/hal/net.h"

namespace Botanik { namespace HAL { namespace Net {

#ifdef TARGET_OPTA
// ---- OPTA backend (Mbed). See Step 5. ----
#else
// ---- Simulator backend (Linux BSD sockets) ----

}}}

#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

namespace Botanik { namespace HAL { namespace Net {

    namespace {
        // Set O_NONBLOCK on a fd; returns the fd (or -1 on failure after closing it).
        int set_nonblock(int fd) {
            int fl = fcntl(fd, F_GETFL, 0);
            if (fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0) { ::close(fd); return -1; }
            return fd;
        }
        sockaddr_in make_sin(const Ip4& a, uint16_t port) {
            sockaddr_in s;
            memset(&s, 0, sizeof(s));
            s.sin_family = AF_INET;
            s.sin_port   = htons(port);
            memcpy(&s.sin_addr, a.b, 4);
            return s;
        }
    }  // namespace

    // Link: the host network is always up. No device to configure.
    void      link_configure(const LinkConfig&) {}
    void      link_disconnect() {}
    LinkState link_status() { return LINK_UP; }
    bool      link_ip(Ip4* out) {
        if (out == 0) return false;
        out->b[0] = 127; out->b[1] = 0; out->b[2] = 0; out->b[3] = 1;   // loopback is enough
        return true;
    }

    // DNS: synchronous getaddrinfo, surfaced through the poll API. The handle encodes the
    // terminal result directly (no in-flight state on the host): 2 == OK, 3 == FAILED.
    // The resolved address is stashed in a one-slot cache keyed by the handle.
    namespace {
        Ip4      g_dns_addr;
        DnsHandle g_dns_next = 1;
    }
    DnsHandle dns_begin(const char* host) {
        if (host == 0 || host[0] == '\0') return 0;
        addrinfo hints; memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        addrinfo* res = 0;
        DnsHandle h = ++g_dns_next;
        if (getaddrinfo(host, 0, &hints, &res) != 0 || res == 0)
            return (h << 2) | 3;   // FAILED
        sockaddr_in* sin = reinterpret_cast<sockaddr_in*>(res->ai_addr);
        memcpy(g_dns_addr.b, &sin->sin_addr, 4);
        freeaddrinfo(res);
        return (h << 2) | 2;       // OK
    }
    DnsResult dns_poll(DnsHandle h, Ip4* out) {
        if ((h & 3) == 2) { if (out) *out = g_dns_addr; return DNS_OK; }
        return DNS_FAILED;
    }
    void dns_cancel(DnsHandle) {}

    UdpSocket udp_open() {
        int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        return fd < 0 ? -1 : set_nonblock(fd);
    }
    bool udp_sendto(UdpSocket s, const Ip4& a, uint16_t port, const void* buf, size_t len) {
        if (s < 0) return false;
        sockaddr_in sin = make_sin(a, port);
        ssize_t n = ::sendto(s, buf, len, 0, reinterpret_cast<sockaddr*>(&sin), sizeof(sin));
        return n == (ssize_t)len;
    }
    int udp_recvfrom(UdpSocket s, void* buf, size_t len, Ip4* from, uint16_t* port) {
        if (s < 0) return -1;
        sockaddr_in sin; socklen_t sl = sizeof(sin);
        ssize_t n = ::recvfrom(s, buf, len, 0, reinterpret_cast<sockaddr*>(&sin), &sl);
        if (n < 0) return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
        if (from) memcpy(from->b, &sin.sin_addr, 4);
        if (port) *port = ntohs(sin.sin_port);
        return (int)n;
    }
    void udp_close(UdpSocket s) { if (s >= 0) ::close(s); }

    TcpSocket tcp_open() {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        return fd < 0 ? -1 : set_nonblock(fd);
    }
    int tcp_connect(TcpSocket s, const Ip4& a, uint16_t port) {
        if (s < 0) return -1;
        sockaddr_in sin = make_sin(a, port);
        int r = ::connect(s, reinterpret_cast<sockaddr*>(&sin), sizeof(sin));
        if (r == 0) return 1;                              // connected at once (loopback)
        if (errno == EINPROGRESS || errno == EALREADY) return 0;
        if (errno == EISCONN) return 1;
        return -1;
    }
    int tcp_send(TcpSocket s, const void* buf, size_t len) {
        if (s < 0) return -1;
        ssize_t n = ::send(s, buf, len, 0);
        if (n < 0) return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
        return (int)n;
    }
    int tcp_recv(TcpSocket s, void* buf, size_t len) {
        if (s < 0) return -1;
        ssize_t n = ::recv(s, buf, len, 0);
        if (n == 0) return -1;                             // peer closed
        if (n < 0)  return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
        return (int)n;
    }
    void tcp_close(TcpSocket s) { if (s >= 0) ::close(s); }

}}}

#endif // TARGET_OPTA / simulator
```

Notes for the implementer:
- The `dns_poll` for an in-progress (non-terminal) handle cannot occur on the sim (resolution is synchronous), so only the OK/FAILED encodings exist. Keep the one-slot `g_dns_addr` cache; the resolver copies the address out immediately on OK.
- `getaddrinfo` is blocking. That is acceptable for the sim (single rare lookups); document it with a one-line comment. Do NOT add a worker thread - YAGNI.

- [ ] **Step 5: Write the OPTA backend of `hal/net.cpp`**

Fill the `#ifdef TARGET_OPTA` branch (between `#ifdef TARGET_OPTA` and `#else` near the top of the file). This relocates exactly the Mbed calls that currently live in `network.cpp`/`mqtt.cpp`/`resolver.cpp`.

```cpp
#include "netsocket/EthernetInterface.h"
#include "netsocket/NetworkInterface.h"
#include "netsocket/SocketAddress.h"
#include "netsocket/UDPSocket.h"
#include "netsocket/TCPSocket.h"
#include <cstring>

namespace Botanik { namespace HAL { namespace Net {

    namespace {
        // The single wired interface, built lazily (not during static init).
        NetworkInterface* eth() { static EthernetInterface e; return &e; }

        // Fixed socket tables: small integer ids map to Mbed socket objects.
        const int MAX_UDP = 2;
        const int MAX_TCP = 2;
        UDPSocket g_udp[MAX_UDP]; bool g_udp_used[MAX_UDP] = {false, false};
        TCPSocket g_tcp[MAX_TCP]; bool g_tcp_used[MAX_TCP] = {false, false};

        // DNS slot table: handle -> {state, addr}. Async callback publishes state.
        const int MAX_DNS = 4;
        struct DnsSlot { volatile uint8_t st; Ip4 addr; int mbed_handle; DnsHandle id; };
        DnsSlot g_dns[MAX_DNS];
        DnsHandle g_dns_next = 1;
        // st: 0 free, 1 pending, 2 ok, 3 failed
        void on_dns(DnsSlot* slot, int result, SocketAddress* a) {
            if (result > 0 && a != 0) {
                nsapi_addr_t na = a->get_addr();
                slot->addr.b[0]=na.bytes[0]; slot->addr.b[1]=na.bytes[1];
                slot->addr.b[2]=na.bytes[2]; slot->addr.b[3]=na.bytes[3];
                HAL::dsb(); slot->st = 2;
            } else { HAL::dsb(); slot->st = 3; }
        }

        SocketAddress make_addr(const Ip4& a, uint16_t port) {
            SocketAddress s; s.set_ip_bytes(a.b, NSAPI_IPv4); s.set_port(port); return s;
        }
        int wouldblock(nsapi_size_or_error_t r) { return r == NSAPI_ERROR_WOULD_BLOCK; }
    }  // namespace

    void link_configure(const LinkConfig& c) {
        NetworkInterface* n = eth();
        n->disconnect();
        n->set_blocking(false);
        if (c.dhcp) { n->set_dhcp(true); }
        else {
            SocketAddress ip, mask, gw;
            ip.set_ip_address(c.ip); mask.set_ip_address(c.netmask); gw.set_ip_address(c.gateway);
            n->set_network(ip, mask, gw);
            SocketAddress dns;
            if (c.dns1[0] && dns.set_ip_address(c.dns1)) n->add_dns_server(dns, 0);
            if (c.dns2[0] && dns.set_ip_address(c.dns2)) n->add_dns_server(dns, 0);
        }
        n->connect();
    }
    void link_disconnect() { eth()->disconnect(); }
    LinkState link_status() {
        nsapi_connection_status_t st = eth()->get_connection_status();
        if (st == NSAPI_STATUS_GLOBAL_UP || st == NSAPI_STATUS_LOCAL_UP) return LINK_UP;
        if (st == NSAPI_STATUS_CONNECTING) return LINK_CONNECTING;
        return LINK_DOWN;
    }
    bool link_ip(Ip4* out) {
        if (!out) return false;
        SocketAddress a;
        if (eth()->get_ip_address(&a) != NSAPI_ERROR_OK) return false;
        nsapi_addr_t na = a.get_addr();
        out->b[0]=na.bytes[0]; out->b[1]=na.bytes[1]; out->b[2]=na.bytes[2]; out->b[3]=na.bytes[3];
        return true;
    }

    DnsHandle dns_begin(const char* host) {
        if (host == 0 || host[0] == '\0') return 0;
        DnsSlot* slot = 0;
        for (int i = 0; i < MAX_DNS; ++i) if (g_dns[i].st == 0) { slot = &g_dns[i]; break; }
        if (slot == 0) return 0;
        slot->id = ++g_dns_next; slot->mbed_handle = 0; slot->st = 1;   // pending before the call
        nsapi_value_or_error_t r = eth()->gethostbyname_async(
            host, mbed::Callback<void(int, SocketAddress*)>(&on_dns, slot), NSAPI_IPv4);
        if (r == NSAPI_ERROR_OK) return slot->id;        // resolved synchronously; st published
        if (r < 0) { slot->st = 0; return 0; }           // failed to start
        slot->mbed_handle = (int)r;
        return slot->id;
    }
    DnsResult dns_poll(DnsHandle h, Ip4* out) {
        for (int i = 0; i < MAX_DNS; ++i) {
            if (g_dns[i].id != h || g_dns[i].st == 0) continue;
            uint8_t st = HAL::ordered_read(&g_dns[i].st);
            if (st == 1) return DNS_PENDING;
            if (st == 2) { if (out) *out = g_dns[i].addr; g_dns[i].st = 0; return DNS_OK; }
            g_dns[i].st = 0; return DNS_FAILED;
        }
        return DNS_FAILED;
    }
    void dns_cancel(DnsHandle h) {
        for (int i = 0; i < MAX_DNS; ++i) {
            if (g_dns[i].id != h || g_dns[i].st == 0) continue;
            if (g_dns[i].st == 1 && g_dns[i].mbed_handle != 0)
                eth()->gethostbyname_async_cancel(g_dns[i].mbed_handle);
            g_dns[i].st = 0; return;
        }
    }

    UdpSocket udp_open() {
        for (int i = 0; i < MAX_UDP; ++i) if (!g_udp_used[i]) {
            g_udp[i].close();
            if (g_udp[i].open(eth()) != NSAPI_ERROR_OK) return -1;
            g_udp[i].set_blocking(false); g_udp_used[i] = true; return i;
        }
        return -1;
    }
    bool udp_sendto(UdpSocket s, const Ip4& a, uint16_t port, const void* buf, size_t len) {
        if (s < 0 || s >= MAX_UDP || !g_udp_used[s]) return false;
        return g_udp[s].sendto(make_addr(a, port), buf, len) == (nsapi_size_or_error_t)len;
    }
    int udp_recvfrom(UdpSocket s, void* buf, size_t len, Ip4* from, uint16_t* port) {
        if (s < 0 || s >= MAX_UDP || !g_udp_used[s]) return -1;
        SocketAddress fa;
        nsapi_size_or_error_t r = g_udp[s].recvfrom(&fa, buf, len);
        if (wouldblock(r)) return 0;
        if (r < 0) return -1;
        if (from) { nsapi_addr_t na = fa.get_addr(); memcpy(from->b, na.bytes, 4); }
        if (port) *port = fa.get_port();
        return (int)r;
    }
    void udp_close(UdpSocket s) { if (s >= 0 && s < MAX_UDP && g_udp_used[s]) { g_udp[s].close(); g_udp_used[s] = false; } }

    TcpSocket tcp_open() {
        for (int i = 0; i < MAX_TCP; ++i) if (!g_tcp_used[i]) {
            g_tcp[i].close();
            if (g_tcp[i].open(eth()) != NSAPI_ERROR_OK) return -1;
            g_tcp[i].set_blocking(false); g_tcp_used[i] = true; return i;
        }
        return -1;
    }
    int tcp_connect(TcpSocket s, const Ip4& a, uint16_t port) {
        if (s < 0 || s >= MAX_TCP || !g_tcp_used[s]) return -1;
        nsapi_error_t r = g_tcp[s].connect(make_addr(a, port));   // re-poll friendly
        if (r == NSAPI_ERROR_OK || r == NSAPI_ERROR_IS_CONNECTED) return 1;
        if (r == NSAPI_ERROR_IN_PROGRESS || r == NSAPI_ERROR_ALREADY) return 0;
        return -1;
    }
    int tcp_send(TcpSocket s, const void* buf, size_t len) {
        if (s < 0 || s >= MAX_TCP || !g_tcp_used[s]) return -1;
        nsapi_size_or_error_t r = g_tcp[s].send(buf, len);
        if (wouldblock(r)) return 0;
        return r < 0 ? -1 : (int)r;
    }
    int tcp_recv(TcpSocket s, void* buf, size_t len) {
        if (s < 0 || s >= MAX_TCP || !g_tcp_used[s]) return -1;
        nsapi_size_or_error_t r = g_tcp[s].recv(buf, len);
        if (wouldblock(r)) return 0;
        if (r == 0) return -1;     // peer closed
        return r < 0 ? -1 : (int)r;
    }
    void tcp_close(TcpSocket s) { if (s >= 0 && s < MAX_TCP && g_tcp_used[s]) { g_tcp[s].close(); g_tcp_used[s] = false; } }

}}}
```

Add `#include "libbotanik/hal/platform.h"` at the top of `net.cpp` (above the namespace) so `HAL::dsb()` / `HAL::ordered_read()` are available to the OPTA branch.

- [ ] **Step 6: Register the new test**

Inspect `tests/CMakeLists.txt` for how test sources are listed (glob vs explicit). If explicit, add `test_hal_net.cpp` to the source list next to `test_network.cpp`. If a glob, no change is needed.

Run: `grep -n "test_network\|GLOB\|test_resolver" tests/CMakeLists.txt`
Then edit if the list is explicit.

- [ ] **Step 7: Build SIMULATOR + run tests**

Run: `cmake -B build -DBOTANIK_TARGET=SIMULATOR >/dev/null && make -C build check 2>&1 | tail -25`
Expected: build succeeds; the four `HAL::Net sim` cases pass.

- [ ] **Step 8: OPTA cross-build (if toolchain available)**

Run: `cmake -B build-opta -DBOTANIK_TARGET=OPTA >/dev/null && make -C build-opta -j$(nproc) 2>&1 | tail -15`
Expected: `net.cpp` compiles against Mbed. If the toolchain is unavailable, note it and defer to Task 6.

- [ ] **Step 9: Commit**

```bash
git add src/libbotanik/hal/net.h src/libbotanik/hal/net.cpp tests/test_hal_net.cpp tests/CMakeLists.txt
git commit -m "hal: add a minimal socket layer (link/dns/udp/tcp), sim uses real BSD sockets"
```

---

### Task 3: Rewire `NameResolver` onto `HAL::Net` DNS

Replace the Mbed async-DNS path with `HAL::Net::dns_*`. The literal fast path is unchanged. Keep `set_interface()`/`interface()` as a vestigial `NetworkInterface*` holder for now (Task 6 removes them, once no consumer is left).

**Files:**
- Modify: `src/libbotanik/network/resolver.h`
- Modify: `src/libbotanik/network/resolver.cpp`
- Test: `tests/test_resolver.cpp`

**Interfaces:**
- Consumes: `HAL::Net::dns_begin/dns_poll/dns_cancel`, `HAL::Net::Ip4` (Task 2).
- Produces: `NameResolver::resolve(const char*, ResolvedAddr*)` unchanged signature/semantics. `Entry::handle` now holds a `HAL::Net::DnsHandle` instead of an Mbed id.

- [ ] **Step 1: Update `test_resolver.cpp` to be offline-deterministic**

The host now has real DNS, so a hostname no longer fails by absence. Use a guaranteed-unresolvable name. Replace the hostname case (lines 49-53):

```cpp
TEST_CASE("NameResolver: a guaranteed-bad hostname resolves to FAILED") {
    NameResolver r;
    ResolvedAddr a;
    // .invalid (RFC 6761) never resolves, online or off - deterministic FAILED.
    CHECK(r.resolve("nonexistent.invalid", &a) == RESOLVE_FAILED);
}
```

Also update the comment in the "malformed literals" case (lines 39-40): the reason is now "not valid literals, and DNS rejects them as names", not "no interface set". Adjust the prose only.

- [ ] **Step 2: Run the test to verify it fails (red)**

Run: `make -C build check 2>&1 | grep -A3 resolver | tail -20`
Expected: with the old `#else` body still returning RESOLVE_FAILED for any name, this actually still passes. To genuinely drive the change, first confirm the suite builds, then proceed - the substantive red/green is the address-family path. Treat Step 4's "literal still OK + bad name FAILED via real getaddrinfo" as the assertion under test.

- [ ] **Step 3: Edit `resolver.h`**

- Remove the Mbed forward declarations (lines 31-32: `class NetworkInterface;` and `class SocketAddress;`).
- Add `#include "libbotanik/hal/net.h"`.
- In `struct Entry`: change `int handle;` to `HAL::Net::DnsHandle handle;` and keep `volatile uint8_t state;`.
- Keep `NetworkInterface* _net;`? No - replace with a plain holder so the type does not reappear. Change the member to `void* _net;` is ugly; instead keep the vestigial accessors but typed as `void*`:

  Replace the member `NetworkInterface* _net;` with nothing - remove it. Replace the two accessor methods with transitional no-ops that other code still compiles against. Since `NetworkHandler` (Task 6) and `TimeSync`/`Mqtt` (Tasks 4-5) still reference them until converted, keep them but make them target-neutral:

  ```cpp
  // Transitional (removed in the NetworkHandler task once no consumer remains):
  // the resolver no longer needs the interface; HAL::Net owns the device.
  void  set_interface(void* /*unused*/) {}
  void* interface() const { return (void*)1; }   // non-null => "configured"; see note
  ```

  NOTE TO IMPLEMENTER: the only consumers of `interface()` are the link-up gates in `TimeSync`/`Mqtt` and the socket-open in their `_send_query`/`_tcp_begin`. Tasks 4 and 5 remove those calls before Task 6 deletes these two methods. Returning a constant non-null keeps the gate "up" on the host during the transition, but Tasks 4-5 land immediately after, so this state is short-lived. If you are executing all tasks in one session, you may instead delete `set_interface`/`interface` here and do Tasks 3-6 as one combined change - see the note at the end of this plan.

- Remove the `_on_dns` static method declaration (lines 117-119).
- Update the file-header doc: drop "Mbed async DNS"; say "via the HAL::Net DNS poll API; the simulator backend uses the host resolver".

- [ ] **Step 4: Edit `resolver.cpp`**

- Remove the `#ifdef TARGET_OPTA ... #endif` Mbed includes (lines 15-18).
- Keep `#include "libbotanik/hal/platform.h"` and add `#include "libbotanik/hal/net.h"`.
- In `clear()`: replace the `#ifdef TARGET_OPTA` cancel block with `HAL::Net::dns_cancel(_table[i].handle)` for PENDING rows (no `#ifdef`):

  ```cpp
  void NameResolver::clear() {
      for (size_t i = 0; i < Config::DNS_CACHE_SIZE; ++i) {
          if (_table[i].state == SLOT_PENDING && _table[i].handle != 0)
              HAL::Net::dns_cancel(_table[i].handle);
          _table[i].state   = SLOT_FREE;
          _table[i].handle  = 0;
          _table[i].host[0] = '\0';
      }
  }
  ```

- Replace the whole `#ifdef TARGET_OPTA ... #else ... #endif` block at the end of `resolve()` (lines 112-147) and the `_on_dns` definition (lines 150-168) with a single target-neutral path:

  ```cpp
      // Start (or poll) a HAL DNS lookup. A row stores its DnsHandle while pending.
      e = _alloc();
      if (e == nullptr) return RESOLVE_FAILED;       // every row is an in-flight lookup

      HAL::Net::DnsHandle h = HAL::Net::dns_begin(host);
      if (h == 0) {                                  // could not start
          if (_logger != nullptr) _logger->warning(PlcIO::NETWORK, "DNS lookup failed to start");
          return RESOLVE_FAILED;
      }
      strncpy(e->host, host, Config::PLC_HOST_STR_LEN - 1);
      e->host[Config::PLC_HOST_STR_LEN - 1] = '\0';
      e->handle = h;
      e->state  = SLOT_PENDING;

      // Poll immediately: the sim resolves synchronously, and OPTA may hit a stack cache.
      HAL::Net::Ip4 ip;
      HAL::Net::DnsResult r = HAL::Net::dns_poll(h, &ip);
      if (r == HAL::Net::DNS_OK) {
          e->addr.bytes[0]=ip.b[0]; e->addr.bytes[1]=ip.b[1];
          e->addr.bytes[2]=ip.b[2]; e->addr.bytes[3]=ip.b[3];
          e->state = SLOT_READY;
          *out = e->addr;
          return RESOLVE_OK;
      }
      if (r == HAL::Net::DNS_FAILED) { e->state = SLOT_FREE; return RESOLVE_FAILED; }
      return RESOLVE_PENDING;
  ```

- Update the `_find`-hit branch (lines 100-110): when a row is `SLOT_PENDING`, poll the HAL handle so a later tick can complete it:

  ```cpp
      Entry* e = _find(host);
      if (e != nullptr) {
          uint8_t st = HAL::ordered_read(&e->state);
          if (st == SLOT_READY)  { *out = e->addr; return RESOLVE_OK; }
          if (st == SLOT_PENDING) {
              HAL::Net::Ip4 ip;
              HAL::Net::DnsResult r = HAL::Net::dns_poll(e->handle, &ip);
              if (r == HAL::Net::DNS_OK) {
                  e->addr.bytes[0]=ip.b[0]; e->addr.bytes[1]=ip.b[1];
                  e->addr.bytes[2]=ip.b[2]; e->addr.bytes[3]=ip.b[3];
                  e->state = SLOT_READY; *out = e->addr; return RESOLVE_OK;
              }
              if (r == HAL::Net::DNS_FAILED) { e->state = SLOT_FREE; return RESOLVE_FAILED; }
              return RESOLVE_PENDING;
          }
          e->state = SLOT_FREE;   // SLOT_ERROR: free so the next poll retries
          return RESOLVE_FAILED;
      }
  ```

  (With this, `SLOT_ERROR` is no longer produced by the resolver itself - `dns_poll` failures free the row directly - but keep the enum value and the defensive branch.)

- [ ] **Step 5: Build + run resolver tests (green)**

Run: `make -C build check 2>&1 | grep -iA3 resolver | tail -20`
Expected: all `NameResolver` cases pass; `192.168.1.10` -> OK (literal), `nonexistent.invalid` -> FAILED (real getaddrinfo).

- [ ] **Step 6: Confirm the file is clean of `TARGET_OPTA`**

Run: `grep -n TARGET_OPTA src/libbotanik/network/resolver.cpp src/libbotanik/network/resolver.h`
Expected: no output.

- [ ] **Step 7: Commit**

```bash
git add src/libbotanik/network/resolver.h src/libbotanik/network/resolver.cpp tests/test_resolver.cpp
git commit -m "resolver: resolve names through HAL::Net DNS instead of Mbed"
```

---

### Task 4: Rewire `TimeSyncHandler` onto `HAL::Net` UDP + a `NetworkHandler` link gate

Replace the Mbed `UDPSocket` SNTP path with `HAL::Net::udp_*`, and gate readiness on `NetworkHandler::is_up()` instead of `resolver->interface()`.

**Files:**
- Modify: `src/libbotanik/network/network.h` (TimeSyncHandler decl)
- Modify: `src/libbotanik/network/network.cpp` (TimeSyncHandler impl)
- Test: `tests/test_network.cpp` (TimeSyncHandler case)

**Interfaces:**
- Consumes: `HAL::Net::udp_open/udp_sendto/udp_recvfrom/udp_close`, `HAL::Net::Ip4`; `NetworkHandler::is_up()` (exists already).
- Produces: `TimeSyncHandler(PlcIO::PlcSyncContext*, Logs::Logger*, NameResolver*, const NetworkHandler*)` - constructor gains a trailing `const NetworkHandler* net = nullptr`.

- [ ] **Step 1: Update the TimeSyncHandler test**

In `tests/test_network.cpp`, the TimeSyncHandler case (lines 80-101) must pass a `NetworkHandler` and use a deterministic server. Rewrite:

```cpp
TEST_CASE("TimeSyncHandler: stepped as a Component (announces once); query is non-blocking") {
    botanik_test::PlcConfigFixture fx;
    NameResolver resolver;
    NetworkHandler net(fx.ctx(), nullptr, &resolver);
    RecordingWriter w;
    Logs::Logger log;
    log.set_min_level(PlcIO::LOG_DEBUG);
    log.add_writer(&w);

    TimeSyncHandler ts(fx.ctx(), &log, &resolver, &net);

    // Bring the (sim) link up so the time sync can proceed past the link gate.
    fx.conf().net.serial = 1; fx.conf().net.dhcp = 1; fx.publish();
    net.step(HAL::uptime_ms());

    fx.conf().ntp.serial  = 1;
    fx.conf().ntp.enabled = 1;
    // TEST-NET-1 literal: a real UDP send goes nowhere, so the reply never arrives and
    // the query stays non-blocking (no DNS, no real NTP traffic).
    std::strncpy(fx.conf().ntp.server1, "192.0.2.1", Config::PLC_HOST_STR_LEN - 1);
    fx.publish();

    for (int i = 0; i < 5; ++i) ts.step(HAL::uptime_ms());   // must not fault or block

#ifdef DEBUG
    CHECK(w.pushes >= 1);
    CHECK(std::string(w.last_msg).find("TimeSyncHandler") != std::string::npos);
#endif
}
```

- [ ] **Step 2: Run to verify it fails (red)**

Run: `make -C build check 2>&1 | tail -25`
Expected: FAIL to compile - `TimeSyncHandler` has no 4-arg constructor.

- [ ] **Step 3: Edit `network.h` (TimeSyncHandler)**

- Add `const NetworkHandler* _net;` to the member block (after `_resolver`). `NetworkHandler` is declared earlier in this same header, so no new include is needed.
- Change the constructor:

  ```cpp
  TimeSyncHandler(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger = nullptr,
                  NameResolver* resolver = nullptr, const NetworkHandler* net = nullptr);
  ```

- Update the `_send_query`/`_poll_query` doc comments: drop "OPTA only".
- Remove the stale `NetworkInterface` mention in the file-header doc.

- [ ] **Step 4: Edit `network.cpp` (TimeSyncHandler)**

- Constructor: initialize `_net(net)`:

  ```cpp
  TimeSyncHandler::TimeSyncHandler(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger,
                                   NameResolver* resolver, const NetworkHandler* net)
      : ConfigHandler(ctx, logger, "TimeSyncHandler"),
        _state(TIME_IDLE), _since_ms(0), _resolver(resolver), _net(net), _server_idx(0) {
      _server_addr.bytes[0] = _server_addr.bytes[1] = 0;
      _server_addr.bytes[2] = _server_addr.bytes[3] = 0;
  }
  ```

  (Place `_net(net)` in declaration order: members are `_state, _since_ms, _resolver, _net, _server_idx` per the header edit above - keep header and initializer order matching.)

- In `on_step` `TIME_RESOLVE`: change the link gate from the resolver interface to the NetworkHandler:

  ```cpp
      // Needs a live link before sending; the NetworkHandler owns that state.
      if (_net == nullptr || !_net->is_up()) break;
  ```

  (Remove the `_resolver == nullptr || _resolver->interface() == nullptr` check; keep the `_resolver` null-guard where `resolve()` is called.)

- Replace the `#ifdef TARGET_OPTA / #else` send block in `TIME_RESOLVE` (lines 237-242) with:

  ```cpp
      if (_send_query()) { _state = TIME_QUERY; _since_ms = now_ms; }
      else               { _next_server_or_error(now_ms); }
  ```

- Replace the `#ifdef TARGET_OPTA ... #endif` body of `TIME_QUERY` (lines 247-258) with the unconditional version (drop the guard, keep the inner logic).

- Replace `_send_query` / `_poll_query` (lines 280-314, currently `#ifdef TARGET_OPTA`) with HAL versions. Note `ntp_to_unix_ms` is in the anonymous namespace currently under `#ifdef TARGET_OPTA` (lines 24-59) - move it OUT of the guard so it is always compiled (it is pure byte math). Likewise drop the `#ifdef TARGET_OPTA` Mbed includes (lines 15-20) entirely.

  ```cpp
  bool TimeSyncHandler::_send_query() {
      HAL::Net::UdpSocket s = HAL::Net::udp_open();
      if (s < 0) return false;
      _udp = s;                                  // store the handle for the poll/close
      HAL::Net::Ip4 srv;
      srv.b[0]=_server_addr.bytes[0]; srv.b[1]=_server_addr.bytes[1];
      srv.b[2]=_server_addr.bytes[2]; srv.b[3]=_server_addr.bytes[3];
      uint8_t pkt[48] = {0};
      pkt[0] = 0x23;                             // LI=0, VN=4, Mode=3 (client)
      if (!HAL::Net::udp_sendto(s, srv, 123, pkt, sizeof(pkt))) { HAL::Net::udp_close(s); _udp = -1; return false; }
      return true;
  }

  int TimeSyncHandler::_poll_query(uint64_t now_ms) {
      uint8_t pkt[48];
      HAL::Net::Ip4 from; uint16_t port = 0;
      int r = HAL::Net::udp_recvfrom(_udp, pkt, sizeof(pkt), &from, &port);
      if (r == 0) {
          if (now_ms - _since_ms > Config::NTP_QUERY_TIMEOUT_MS) { HAL::Net::udp_close(_udp); _udp = -1; return -1; }
          return 0;
      }
      if (r < (int)sizeof(pkt)) { HAL::Net::udp_close(_udp); _udp = -1; return -1; }
      uint64_t unix_ms = ntp_to_unix_ms(pkt);
      HAL::Net::udp_close(_udp); _udp = -1;
      if (unix_ms == 0) return -1;
      HAL::wall_clock_set(unix_ms);
      return 1;
  }
  ```

- Add the `_udp` member to the TimeSyncHandler decl in `network.h`: `HAL::Net::UdpSocket _udp;` (init to `-1` in the constructor). Add `#include "libbotanik/hal/net.h"` to `network.h`.

- [ ] **Step 5: Build + run (green)**

Run: `make -C build check 2>&1 | tail -25`
Expected: build succeeds; the TimeSyncHandler case passes (announces, does not block on the TEST-NET send).

- [ ] **Step 6: Confirm no `TARGET_OPTA` remains in the TimeSync code paths**

Run: `grep -n "TARGET_OPTA" src/libbotanik/network/network.cpp`
Expected: only the NetworkHandler bring-up uses remain (removed in Task 6); the TimeSync section is clean.

- [ ] **Step 7: Commit**

```bash
git add src/libbotanik/network/network.h src/libbotanik/network/network.cpp tests/test_network.cpp
git commit -m "timesync: run SNTP over HAL::Net UDP, gate on NetworkHandler link state"
```

---

### Task 5: Rewire `MqttClient` onto `HAL::Net` TCP + a `NetworkHandler` link gate

**Files:**
- Modify: `src/libbotanik/network/mqtt.h`
- Modify: `src/libbotanik/network/mqtt.cpp`
- Test: `tests/test_network.cpp` (MqttClient case)

**Interfaces:**
- Consumes: `HAL::Net::tcp_open/tcp_connect/tcp_send/tcp_recv/tcp_close`, `HAL::Net::Ip4`; `NetworkHandler::is_up()`.
- Produces: `MqttClient(PlcIO::PlcSyncContext*, Logs::Logger*, NameResolver*, const NetworkHandler*)` - constructor gains trailing `const NetworkHandler* net = nullptr`. `connected()`/`publish()` signatures unchanged.

- [ ] **Step 1: Update the MqttClient test**

In `tests/test_network.cpp`, rewrite the MqttClient case (lines 61-78) to pass a live `NetworkHandler` and a TEST-NET broker:

```cpp
TEST_CASE("MqttClient: never reports connected to a dead broker; publish gated on connection") {
    botanik_test::PlcConfigFixture fx;
    NameResolver resolver;
    NetworkHandler net(fx.ctx(), nullptr, &resolver);
    MqttClient mqtt(fx.ctx(), nullptr, &resolver, &net);

    CHECK_FALSE(mqtt.connected());
    CHECK_FALSE(mqtt.publish("botanik/alarms/X", "ON"));

    fx.conf().net.serial = 1; fx.conf().net.dhcp = 1; fx.publish();
    net.step(HAL::uptime_ms());                 // sim link up

    fx.conf().mqtt.serial  = 1;
    fx.conf().mqtt.enabled = 1;
    std::strncpy(fx.conf().mqtt.broker, "192.0.2.1", Config::PLC_HOST_STR_LEN - 1);  // TEST-NET-1
    fx.publish();
    for (int i = 0; i < 5; ++i) mqtt.step(HAL::uptime_ms());

    CHECK_FALSE(mqtt.connected());              // connect to 192.0.2.1 never completes
    CHECK_FALSE(mqtt.publish("botanik/alarms/X", "ON"));
}
```

(Add `#include "libbotanik/network/network.h"` to the test if not already present - it is, line 18.)

- [ ] **Step 2: Run to verify it fails (red)**

Run: `make -C build check 2>&1 | tail -25`
Expected: FAIL to compile - `MqttClient` has no 4-arg constructor.

- [ ] **Step 3: Edit `mqtt.h`**

- Add `#include "libbotanik/network/network.h"` (for `NetworkHandler`) and `#include "libbotanik/hal/net.h"`.
- Add members after `_resolver`: `const NetworkHandler* _net;` and `HAL::Net::TcpSocket _sock;`.
- Change the constructor decl:

  ```cpp
  MqttClient(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger = nullptr,
             NameResolver* resolver = nullptr, const NetworkHandler* net = nullptr);
  ```

- Update the file-header / member doc comments to drop "Mbed TCPSocket ... OPTA only" and "the resolver holds the live interface".

- [ ] **Step 4: Edit `mqtt.cpp`**

- Drop the `#ifdef TARGET_OPTA` Mbed includes (lines 16-19).
- Move the anonymous-namespace `mqtt_socket()` helper out (it is replaced by `_sock`), but KEEP `encode_remlen` (pure) - take it out of the `#ifdef TARGET_OPTA` guard so it is always compiled.
- Constructor: init `_net(net)` and `_sock(-1)` in declaration order.
- `_fail`: replace the `#ifdef TARGET_OPTA mqtt_socket().close();` with `if (_sock >= 0) { HAL::Net::tcp_close(_sock); _sock = -1; }`.
- `on_config_changed`: same close replacement.
- `on_step` `MQTT_RESOLVE`: change the link gate to `if (_net == nullptr || !_net->is_up()) break;` (was `_resolver->interface()`), keep the `_resolver` null-guard for `resolve()`. Replace the `#ifdef TARGET_OPTA / #else` connect block (lines 95-100) with:

  ```cpp
      if (_tcp_begin()) _enter(MQTT_CONNECT, now_ms);
      else              _fail(now_ms);
  ```

- Remove the `#ifdef TARGET_OPTA` guards around `MQTT_CONNECT`, `MQTT_HANDSHAKE`, `MQTT_READY` bodies (lines 105-135) - keep the inner logic unconditional.
- `publish`: replace the `#ifdef TARGET_OPTA / #else` body (lines 146-167) with the HAL version; the packet construction is identical, only the socket call changes:

  ```cpp
  bool MqttClient::publish(const char* topic, const char* payload, bool retain) {
      if (_state != MQTT_READY || topic == nullptr || payload == nullptr) return false;
      uint16_t tlen = (uint16_t)strlen(topic);
      uint32_t plen = (uint32_t)strlen(payload);
      uint8_t buf[Config::MQTT_TX_BUF];
      uint32_t remlen = 2u + tlen + plen;
      int i = 0;
      buf[i++] = retain ? 0x31 : 0x30;
      i += encode_remlen(&buf[i], remlen);
      buf[i++] = (uint8_t)(tlen >> 8);
      buf[i++] = (uint8_t)(tlen & 0xFF);
      if ((size_t)i + tlen + plen > sizeof(buf)) return false;
      memcpy(&buf[i], topic, tlen);    i += tlen;
      memcpy(&buf[i], payload, plen);  i += (int)plen;
      if (HAL::Net::tcp_send(_sock, buf, i) != i) { _fail(HAL::uptime_ms()); return false; }
      return true;
  }
  ```

- Replace the OPTA-only helpers `_tcp_begin/_tcp_poll/_send_connect/_poll_connack/_service` (lines 170-259, under `#ifdef TARGET_OPTA`) with HAL versions (drop the guard). Key replacements (the wire-protocol byte construction is unchanged; only the transport calls change):

  ```cpp
  bool MqttClient::_tcp_begin() {
      if (_sock >= 0) HAL::Net::tcp_close(_sock);
      _sock = HAL::Net::tcp_open();
      if (_sock < 0) return false;
      const PlcIO::PlcOutputConfig_t* c = config();
      uint16_t port = (c && c->mqtt.port) ? c->mqtt.port : Config::MQTT_PORT_DEFAULT;
      HAL::Net::Ip4 a;
      a.b[0]=_broker_addr.bytes[0]; a.b[1]=_broker_addr.bytes[1];
      a.b[2]=_broker_addr.bytes[2]; a.b[3]=_broker_addr.bytes[3];
      int r = HAL::Net::tcp_connect(_sock, a, port);   // 1 done, 0 pending, <0 err
      if (r >= 0) return true;
      HAL::Net::tcp_close(_sock); _sock = -1;
      return false;
  }

  int MqttClient::_tcp_poll() {
      const PlcIO::PlcOutputConfig_t* c = config();
      uint16_t port = (c && c->mqtt.port) ? c->mqtt.port : Config::MQTT_PORT_DEFAULT;
      HAL::Net::Ip4 a;
      a.b[0]=_broker_addr.bytes[0]; a.b[1]=_broker_addr.bytes[1];
      a.b[2]=_broker_addr.bytes[2]; a.b[3]=_broker_addr.bytes[3];
      return HAL::Net::tcp_connect(_sock, a, port);    // 1 connected, 0 waiting, -1 failed
  }

  bool MqttClient::_send_connect() {
      // ... identical packet construction into buf[] ...
      return HAL::Net::tcp_send(_sock, buf, i) == i;
  }

  int MqttClient::_poll_connack() {
      uint8_t buf[4];
      int r = HAL::Net::tcp_recv(_sock, buf, sizeof(buf));
      if (r == 0) return 0;            // would-block
      if (r < 4) return -1;
      if (buf[0] != 0x20 || buf[3] != 0x00) return -1;
      return 1;
  }

  void MqttClient::_service(uint64_t now_ms) {
      uint8_t scratch[32];
      for (;;) {
          int r = HAL::Net::tcp_recv(_sock, scratch, sizeof(scratch));
          if (r == 0) break;           // would-block: drained
          if (r < 0) { _fail(now_ms); return; }   // closed/error
      }
      const PlcIO::PlcOutputConfig_t* c = config();
      uint32_t ka_s = (c && c->mqtt.keepalive_s) ? c->mqtt.keepalive_s : Config::MQTT_KEEPALIVE_DEFAULT_S;
      if (now_ms - _last_ping_ms > (uint64_t)ka_s * 1000ull / 2ull) {
          uint8_t ping[2] = { 0xC0, 0x00 };
          if (HAL::Net::tcp_send(_sock, ping, 2) != 2) { _fail(now_ms); return; }
          _last_ping_ms = now_ms;
      }
  }
  ```

  (For `_send_connect`, copy the existing CONNECT-packet byte construction verbatim from the current OPTA body; only the final `send` line changes as shown.)

- [ ] **Step 5: Build + run (green)**

Run: `make -C build check 2>&1 | tail -25`
Expected: build succeeds; MqttClient case passes (never connects to 192.0.2.1; publish refused).

- [ ] **Step 6: Confirm mqtt is clean**

Run: `grep -n TARGET_OPTA src/libbotanik/network/mqtt.cpp src/libbotanik/network/mqtt.h`
Expected: no output.

- [ ] **Step 7: Commit**

```bash
git add src/libbotanik/network/mqtt.h src/libbotanik/network/mqtt.cpp tests/test_network.cpp
git commit -m "mqtt: publish over HAL::Net TCP, gate on NetworkHandler link state"
```

---

### Task 6: Rewire `NetworkHandler` onto `HAL::Net` link + remove the vestigial interface API

Last consumer. Converts bring-up to `HAL::Net::link_*`, then deletes `set_interface()`/`interface()` and the `NetworkInterface` forward declaration now that nothing uses them. Finishes with the full guard check + OPTA cross-build.

**Files:**
- Modify: `src/libbotanik/network/network.h` (NetworkHandler decl)
- Modify: `src/libbotanik/network/network.cpp` (NetworkHandler impl)
- Modify: `src/libbotanik/network/resolver.h` (drop the transitional `set_interface`/`interface`)
- Test: `tests/test_network.cpp` (NetworkHandler case - should already pass; verify)

**Interfaces:**
- Consumes: `HAL::Net::link_configure/link_disconnect/link_status/link_ip` (Task 2).
- Produces: `NetworkHandler` with `is_up()` unchanged as the authoritative link-state accessor; `interface()` removed.

- [ ] **Step 1: Edit `network.h` (NetworkHandler)**

- Remove the member `NetworkInterface* _net;` (line 47).
- Remove the `interface()` accessor (lines 62-63). Keep `is_up()`.
- Remove the global `class NetworkInterface;` forward declaration (line 23).
- Update `_begin_setup` / `_unwire_resolver` doc comments: bring-up is via HAL; `_unwire_resolver` now just clears the resolver cache.
- Update the class doc: "Owns the link (the authoritative `is_up()`); drives `HAL::Net` bring-up."

- [ ] **Step 2: Edit `network.cpp` (NetworkHandler)**

- Constructor: drop `_net(nullptr)` from the initializer list.
- `_unwire_resolver()`: drop the `_resolver->set_interface(nullptr)` call; keep `_resolver->clear()`:

  ```cpp
  void NetworkHandler::_unwire_resolver() {
      if (_resolver != nullptr) _resolver->clear();
  }
  ```

- `on_config_changed`: replace the `#ifdef TARGET_OPTA _net->disconnect();` (lines 93-95) with `HAL::Net::link_disconnect();` (no guard).
- `_begin_setup` (lines 102-133): replace the entire `#ifdef TARGET_OPTA / #else` body with a single HAL path:

  ```cpp
  void NetworkHandler::_begin_setup(uint64_t now_ms) {
      const PlcIO::PlcOutputConfig_t* c = config();
      HAL::Net::LinkConfig lc;
      memset(&lc, 0, sizeof(lc));
      lc.dhcp = c->net.dhcp != 0;
      if (!lc.dhcp) {
          strncpy(lc.ip,      c->net.ip,      sizeof(lc.ip) - 1);
          strncpy(lc.netmask, c->net.netmask, sizeof(lc.netmask) - 1);
          strncpy(lc.gateway, c->net.gateway, sizeof(lc.gateway) - 1);
          strncpy(lc.dns1,    c->net.dns1,    sizeof(lc.dns1) - 1);
          strncpy(lc.dns2,    c->net.dns2,    sizeof(lc.dns2) - 1);
      }
      HAL::Net::link_configure(lc);
      if (_logger) _logger->info(PlcIO::NETWORK, lc.dhcp ? "network bring-up (DHCP)"
                                                         : "network bring-up (static)");
      _enter(NET_SETUP, now_ms);
  }
  ```

  (On the sim, `link_status()` returns `LINK_UP` immediately, so the very next `on_step` promotes SETUP -> RUNNING - preserving the old "up at once on the host" behavior without a target branch.)

- `on_step` `NET_SETUP` (lines 140-160): replace the `#ifdef TARGET_OPTA` body with a HAL status poll:

  ```cpp
      case NET_SETUP:
          if (HAL::Net::link_status() == HAL::Net::LINK_UP) {
              _enter(NET_RUNNING, now_ms);
              HAL::Net::Ip4 a;
              if (_logger) {
                  if (HAL::Net::link_ip(&a))
                      _logger->notice(PlcIO::NETWORK, "network up: %u.%u.%u.%u",
                                      a.b[0], a.b[1], a.b[2], a.b[3]);
                  else
                      _logger->notice(PlcIO::NETWORK, "network up");
              }
          } else if (now_ms - _since_ms > Config::NET_SETUP_TIMEOUT_MS) {
              if (_logger) _logger->warning(PlcIO::NETWORK, "network bring-up timed out");
              _enter(NET_ERROR, now_ms);
          }
          break;
  ```

  (Drop the resolver `set_interface` calls that were here - the resolver no longer holds an interface.)

- `on_step` `NET_RUNNING` (lines 162-174): replace with a HAL link-loss watch:

  ```cpp
      case NET_RUNNING:
          if (HAL::Net::link_status() != HAL::Net::LINK_UP) {
              if (_logger) _logger->warning(PlcIO::NETWORK, "network link lost");
              _unwire_resolver();
              _enter(NET_ERROR, now_ms);
          }
          break;
  ```

  NOTE: on the sim `link_status()` is always `LINK_UP`, so NET_RUNNING is stable (matches today). Good.

- Drop the `#ifdef TARGET_OPTA` Mbed includes at the top of `network.cpp` (lines 15-20) if not already removed in Task 4. Ensure `#include "libbotanik/hal/net.h"` and `<cstring>` are present.

- [ ] **Step 3: Edit `resolver.h` - remove the transitional API**

- Delete the `set_interface(void*)` and `interface()` methods added in Task 3.
- Confirm no `NetworkInterface`/`void* _net` member remains.

- [ ] **Step 4: Build + run the full suite (green)**

Run: `cmake -B build -DBOTANIK_TARGET=SIMULATOR >/dev/null && make -C build check 2>&1 | tail -30`
Expected: build succeeds; ALL tests pass, including the NetworkHandler "up on config / idle when cleared" case.

- [ ] **Step 5: The guard - `TARGET_OPTA` only under `hal/`**

Run: `grep -rn TARGET_OPTA src --include=*.cpp --include=*.h | grep -v 'src/libbotanik/hal/'`
Expected: NO output. If anything prints, fix it before committing.

- [ ] **Step 6: OPTA cross-build**

Run: `cmake -B build-opta -DBOTANIK_TARGET=OPTA >/dev/null && make -C build-opta -j$(nproc) 2>&1 | tail -20`
Expected: clean cross-build. If the toolchain is unavailable in this environment, state that explicitly in the commit/PR and mark this step as not-verified-here.

- [ ] **Step 7: Update docs (CLAUDE.md HAL note)**

In `CLAUDE.md`, the HAL paragraph (the `hal/` bullet under Architecture) currently lists timing/ordering. Append one sentence:

> The HAL also owns the network transport (`hal/net.{h,cpp}`: link, DNS, UDP, TCP); the simulator backend uses real BSD sockets. `TARGET_OPTA` appears only under `hal/`; shared code never branches on it (`TARGET_SIMULATOR` may, for dev-only scaffolding).

- [ ] **Step 8: Commit**

```bash
git add src/libbotanik/network/network.h src/libbotanik/network/network.cpp \
        src/libbotanik/network/resolver.h CLAUDE.md
git commit -m "network: bring the link up through HAL::Net; confine TARGET_OPTA to the HAL"
```

---

## Combined-execution note

Tasks 3-6 are split so the tree builds after each commit, which requires the
transitional `set_interface`/`interface` shim in Task 3. If you execute Tasks 3-6
back-to-back in a single session, you may instead **delete** `set_interface`/
`interface` and the `NetworkInterface` forward declaration in Task 3 and update all
consumers at once, skipping the shim. Either way, the END state is identical and the
guard in Task 6 Step 5 must pass.

## Self-Review

- **Spec coverage:**
  - Rule (TARGET_OPTA only in hal/) -> Tasks 3-6 remove every non-hal use; Task 6 Step 5 verifies. TARGET_SIMULATOR left alone (Global Constraints). Covered.
  - HAL socket layer (`hal/net.h`+`hal/net.cpp`, Mbed/BSD confined) -> Task 2. Covered.
  - Sim uses real BSD sockets -> Task 2 Step 4. Covered.
  - NameResolver drops interface role -> Task 3 (+ Task 6 removes the shim). Covered.
  - NetworkHandler owns the link (`is_up()` authoritative) -> Task 6. Covered.
  - TimeSync/Mqtt gate on NetworkHandler, sockets via HAL -> Tasks 4, 5. Covered.
  - `libbotanik_sizes.h` flip -> Task 1. Covered.
  - Verification (both builds + grep guard) -> Task 6 Steps 4-6, and per-task builds. Covered.
- **Placeholder scan:** No TBD/TODO; `_send_connect` references "copy the existing CONNECT byte construction verbatim" with the exact source location and the single changed line shown - acceptable (the bytes are unchanged and already in the repo).
- **Type consistency:** `HAL::Net::Ip4{b[4]}`, `ResolvedAddr{bytes[4]}`, `DnsHandle`/`DnsResult`, `UdpSocket`/`TcpSocket`, constructor signatures `(ctx, logger, resolver, net)`, members `_net`/`_udp`/`_sock` are used consistently across Tasks 2-6.
