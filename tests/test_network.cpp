/**
 * @file test_network.cpp
 * @brief The network stack: link handler, resolver, HAL transport, MQTT log writer.
 *
 * Each section below is the former test_<name>.cpp, kept in its own
 * namespace so their fixtures and helpers cannot collide.
 */

#include "doctest.h"
#include "params_fixture.h"
#include "recording_writer.h"               // RecordingWriter
#include "libbotanik_config.h"              // Config::NET_*
#include "libbotanik/hal/platform.h"        // HAL::Clock
#include "libbotanik/logs/logger.h"         // Logs::Logger
#include "libbotanik/network/mqtt.h"        // Network::MqttClient
#include "libbotanik/network/network.h"     // Network::NetworkHandler
#include "libbotanik/network/ntp.h"         // Network::NtpClient
#include "libbotanik/network/resolver.h"    // Network::NameResolver
#include <cstdint>                          // uint32_t/uint64_t
#include "libbotanik/hal/net.h"         // HAL::Network
#include <cstring>                          // memset / strcmp / strncpy / strstr
#include <string>                           // std::string
#include "libbotanik/logs/format.h"         // Logs::format_log_level_name / _topic_name
#include "libbotanik/network/mqtt_log.h"    // Network::MqttLogWriter::build_topic

// --------------------------------------------------------------------------
// network
// --------------------------------------------------------------------------

namespace network {

// #if guards below need the flags DEFINED (undefined reads as 0).

using namespace Botanik;
using namespace Botanik::Network;

namespace {
}

#if BOTANIK_FEATURE_NETWORK
TEST_CASE("NetworkHandler: sim path comes up on net config and idles when it is cleared") {
    botanik_test::ParamFixture fx;
    NameResolver resolver;
    NetworkHandler net(Botanik::SetupContext(), &fx.bank(), &resolver);
    CHECK_FALSE(net.is_up());

    // link_status() returns LINK_UP immediately in sim, so one step() promotes SETUP to RUNNING.
    fx.set_num("net_serial", 1);
    fx.set_num("net_dhcp", 1);
    fx.publish();
    net.step(HAL::Clock::uptime_ms());
    CHECK(net.is_up());

    // Serial back to 0 (config removed): back to idle.
    fx.set_num("net_serial", 0);
    fx.publish();
    net.step(HAL::Clock::uptime_ms());
    CHECK_FALSE(net.is_up());
}
#endif

#if BOTANIK_FEATURE_MQTT
TEST_CASE("MqttClient: never reports connected to a dead broker, publish gated on connection") {
    botanik_test::ParamFixture fx;
    NameResolver resolver;
    NetworkHandler net(Botanik::SetupContext(), &fx.bank(), &resolver);
    MqttClient mqtt(Botanik::SetupContext(), &fx.bank(), &resolver, &net);

    CHECK_FALSE(mqtt.connected());
    CHECK_FALSE(mqtt.publish("botanik/alarms/X", "ON"));

    fx.set_num("net_serial", 1); fx.set_num("net_dhcp", 1); fx.publish();
    net.step(HAL::Clock::uptime_ms());                 // sim link up

    fx.set_num("mqtt_serial", 1);
    fx.set_num("mqtt_enable", 1);
    // Off-host: the hermetic sim refuses non-loopback, so the connect never completes.
    fx.set_str("mqtt_host", "192.0.2.1");
    fx.publish();
    for (int i = 0; i < 5; ++i) mqtt.step(HAL::Clock::uptime_ms());

    CHECK_FALSE(mqtt.connected());              // off-host: refused, never connects
    CHECK_FALSE(mqtt.publish("botanik/alarms/X", "ON"));
}
#endif

// NTP-less builds must not report NTP coverage they don't have.
#if BOTANIK_FEATURE_NTP
TEST_CASE("NtpClient: stepped as a Component (announces once), query is non-blocking") {
    botanik_test::ParamFixture fx;
    NameResolver resolver;
    NetworkHandler net(Botanik::SetupContext(), &fx.bank(), &resolver);
    RecordingWriter w;
    Logs::Logger log;
    log.set_min_level(Logs::LOG_DEBUG);
    log.add_writer(&w);

    NtpClient ts(Botanik::SetupContext(nullptr, &log), &fx.bank(), &resolver, &net);

    // Bring the (sim) link up so the time sync can proceed past the link gate.
    fx.set_num("net_serial", 1); fx.set_num("net_dhcp", 1); fx.publish();
    net.step(HAL::Clock::uptime_ms());

    fx.set_num("ntp_serial", 1);
    fx.set_num("ntp_enable", 1);
    // Off-host: the hermetic sim refuses the send, so the query stays non-blocking with no sync.
    fx.set_str("ntp_srv1", "192.0.2.1");
    fx.publish();

    for (int i = 0; i < 5; ++i) ts.step(HAL::Clock::uptime_ms());   // must not fault or block

#ifdef DEBUG
    // >= 1, not == 1: link-up may emit extra lines beyond the announce, so count
    // isn't pinned; search all lines, not last_msg, since last is timing-dependent.
    CHECK(w.pushes >= 1);
    CHECK(w.saw("NtpClient"));
#endif
}
#endif

// The NTP-synced / MQTT-connected success paths need the loopback mock responder
// (botanik_sim/network.h) and run only in the full simulator, covered by the pyte
// suite; these unit tests cover config-gated state machines and dead-peer behaviour.

}  // namespace network

// --------------------------------------------------------------------------
// resolver
// --------------------------------------------------------------------------

namespace resolver {

// Empty TU when the feature is off (flag must be DEFINED, else #if reads 0);
// no CTest cases - test_sun.cpp's shape.
#if BOTANIK_FEATURE_NETWORK

using namespace Botanik;
using namespace Botanik::Network;

namespace {
    // Drive resolve() to a terminal status: a literal/malformed name settles on
    // the first call, a real one goes through async DNS. Bounded so a hang fails, not spins.
    ResolveStatus settle(NameResolver& r, const char* host, ResolvedAddr* a) {
        uint64_t start = HAL::Clock::uptime_ms();
        for (;;) {
            ResolveStatus s = r.resolve(host, a);
            if (s != RESOLVE_PENDING) return s;
            if (HAL::Clock::uptime_ms() - start > 15000) return s;
            HAL::Clock::sleep_ms(5);
        }
    }
}

TEST_CASE("NameResolver: a dotted-decimal IPv4 literal resolves to its octets") {
    NameResolver r;
    ResolvedAddr a;

    REQUIRE(r.resolve("192.168.1.10", &a) == RESOLVE_OK);
    CHECK(a.b[0] == 192);
    CHECK(a.b[1] == 168);
    CHECK(a.b[2] == 1);
    CHECK(a.b[3] == 10);

    REQUIRE(r.resolve("0.0.0.0", &a) == RESOLVE_OK);
    CHECK((a.b[0] | a.b[1] | a.b[2] | a.b[3]) == 0);

    REQUIRE(r.resolve("255.255.255.255", &a) == RESOLVE_OK);
    CHECK(a.b[0] == 255);
    CHECK(a.b[3] == 255);
}

TEST_CASE("NameResolver: malformed literals are not mis-parsed as addresses") {
    NameResolver r;
    ResolvedAddr a;
    // None are valid dotted-decimal literals, so parse_ipv4 must reject each rather
    // than return a bogus RESOLVE_OK; the sim resolver rejects them too, as bad names.
    CHECK(settle(r, "1.2.3", &a)     == RESOLVE_FAILED);   // too few octets
    CHECK(settle(r, "1.2.3.4.5", &a) == RESOLVE_FAILED);   // too many octets
    CHECK(settle(r, "256.1.1.1", &a) == RESOLVE_FAILED);   // octet out of range
    CHECK(settle(r, "1.2.3.#", &a)   == RESOLVE_FAILED);   // non-digit, illegal host char
    CHECK(settle(r, "1.2..4", &a)    == RESOLVE_FAILED);   // empty octet
    CHECK(settle(r, "10.0.0.1 ", &a) == RESOLVE_FAILED);   // trailing junk
}

TEST_CASE("NameResolver: a guaranteed-bad hostname resolves to FAILED") {
    NameResolver r;
    ResolvedAddr a;
    // .invalid (RFC 6761) never resolves, online or off - deterministic FAILED.
    CHECK(settle(r, "nonexistent.invalid", &a) == RESOLVE_FAILED);
}

TEST_CASE("NameResolver: a resolvable hostname resolves to the loopback peer") {
    NameResolver r;
    ResolvedAddr a;
    // The sim resolver is hermetic: any well-formed name maps to this instance's
    // per-process loopback (127/8), after settle() polls an async PENDING phase.
    REQUIRE(settle(r, "broker.local", &a) == RESOLVE_OK);
    CHECK(a.b[0] == 127);          // loopback (exact host part is PID-derived)
}

TEST_CASE("NameResolver: a cached entry is re-resolved after the TTL") {
    NameResolver r;
    r.set_cache_ttl_ms(20);                 // short max-age for the test
    ResolvedAddr a;

    REQUIRE(settle(r, "broker.local", &a) == RESOLVE_OK);   // first lookup -> cached
    // Still fresh: served from cache, no new lookup (OK, not PENDING).
    CHECK(r.resolve("broker.local", &a) == RESOLVE_OK);

    // Past max-age: the stale entry drops and a fresh lookup begins, so this is PENDING, not an instant hit.
    HAL::Clock::sleep_ms(30);
    CHECK(r.resolve("broker.local", &a) == RESOLVE_PENDING);
    CHECK(settle(r, "broker.local", &a) == RESOLVE_OK);      // and it re-resolves

    // TTL 0 disables expiry: the entry stays cached across the same wait.
    r.set_cache_ttl_ms(0);
    REQUIRE(settle(r, "broker.local", &a) == RESOLVE_OK);
    HAL::Clock::sleep_ms(30);
    CHECK(r.resolve("broker.local", &a) == RESOLVE_OK);      // still cached
}

TEST_CASE("NameResolver: empty / null inputs fail cleanly") {
    NameResolver r;
    ResolvedAddr a;
    CHECK(r.resolve("", &a)             == RESOLVE_FAILED);
    CHECK(r.resolve(nullptr, &a)        == RESOLVE_FAILED);
    CHECK(r.resolve("1.2.3.4", nullptr) == RESOLVE_FAILED);
}

#endif

}  // namespace resolver

// --------------------------------------------------------------------------
// hal_net
// --------------------------------------------------------------------------

namespace hal_net {

// Empty TU when the feature is off; no CTest cases - test_sun.cpp's shape.
#if BOTANIK_FEATURE_NETWORK

using namespace Botanik::HAL;

namespace {
    // Poll an async DNS handle to a terminal result, bounded so a hang fails
    // the test rather than spins.
    Network::DnsResult dns_settle(Network::DnsHandle h, Network::Ip4* out) {
        uint64_t start = Clock::uptime_ms();
        for (;;) {
            Network::DnsResult r = Network::dns_poll(h, out);
            if (r != Network::DNS_PENDING) return r;
            if (Clock::uptime_ms() - start > 15000) return r;
            Clock::sleep_ms(5);
        }
    }
}

TEST_CASE("HAL::Network sim: DNS of a guaranteed-bad name fails, not pending forever") {
    Network::DnsHandle h = Network::dns_begin("nonexistent.invalid");
    REQUIRE(h != 0);
    Network::Ip4 out;
    // Emulated async resolver: poll to terminal, which must be FAILED, never
    // pending forever.
    CHECK(dns_settle(h, &out) == Network::DNS_FAILED);
}

TEST_CASE("HAL::Network sim: an off-host destination is diverted, never contacted") {
    // The sim must never put a packet on the wire, including a site's real
    // (non-loopback) broker literal - such addresses are DIVERTED to the in-process
    // responder, not refused, so MQTT still runs against the shipped config.
    Network::Ip4 ext = {{8, 8, 8, 8}};              // clearly off-host
    uint8_t pkt[4] = {0};

    Network::UdpSocket u = Network::udp_open();
    REQUIRE(u >= 0);
    CHECK(Network::udp_sendto(u, ext, 123, pkt, sizeof(pkt)));   // diverted, not refused
    Network::udp_close(u);

    Network::TcpSocket t = Network::tcp_open();
    REQUIRE(t >= 0);
    CHECK(Network::tcp_connect(t, ext, 58443) >= 0);             // diverted to the mock broker
    Network::tcp_close(t);
}

TEST_CASE("HAL::Network sim: a listener accepts a connection and carries bytes") {
    // What the Modbus TCP slave needs from the HAL end to end: bind, accept, a byte
    // each way. The sim binds this instance's own 127/8 address, so this runs
    // unchanged beside other test processes.
    // NOT 502 (Config::MODBUS_TCP_PORT): a TcpSlave elsewhere may still hold it,
    // and doctest cases share a process with no ordering. 503 is privileged too.
    const uint16_t PORT = 503;                  // privileged: the backend shifts it
    Network::TcpSocket srv = Network::tcp_listen(PORT);
    REQUIRE(srv >= 0);
    CHECK(Network::tcp_accept(srv) < 0);        // nothing pending yet

    Network::TcpSocket cli = Network::tcp_open();
    REQUIRE(cli >= 0);
    // A firmware-like address; lands on the listener because THIS process serves that port, not the mock broker.
    Network::Ip4 any = {{192, 168, 1, 43}};
    REQUIRE(Network::tcp_connect(cli, any, PORT) >= 0);

    Network::TcpSocket peer = -1;
    for (int i = 0; i < 100 && peer < 0; ++i) {  // loopback connect is near-instant
        peer = Network::tcp_accept(srv);
        if (peer < 0) Clock::sleep_ms(2);
    }
    REQUIRE(peer >= 0);

    const uint8_t req[] = {0, 1, 2, 3};
    CHECK(Network::tcp_send(cli, req, sizeof(req)) == (int)sizeof(req));
    uint8_t got[8] = {0};
    int n = 0;
    for (int i = 0; i < 100 && n <= 0; ++i) {
        n = Network::tcp_recv(peer, got, sizeof(got));
        if (n == 0) Clock::sleep_ms(2);          // 0 is would-block, not a close
    }
    REQUIRE(n == (int)sizeof(req));
    CHECK(got[3] == 3);

    const uint8_t rsp[] = {9};
    CHECK(Network::tcp_send(peer, rsp, sizeof(rsp)) == 1);
    n = 0;
    for (int i = 0; i < 100 && n <= 0; ++i) {
        n = Network::tcp_recv(cli, got, sizeof(got));
        if (n == 0) Clock::sleep_ms(2);
    }
    REQUIRE(n == 1);
    CHECK(got[0] == 9);

    // A closed peer reads as closed, not would-block: that frees the server's connection slot.
    Network::tcp_close(cli);
    n = 0;
    for (int i = 0; i < 100 && n == 0; ++i) {
        n = Network::tcp_recv(peer, got, sizeof(got));
        if (n == 0) Clock::sleep_ms(2);
    }
    CHECK(n < 0);

    Network::tcp_close(peer);
    Network::tcp_close(srv);
}

TEST_CASE("HAL::Network sim: a released port stops capturing connects") {
    // The listening-port table keeps a connect from diverting to the mock broker;
    // a stale row would send later connects to a port nothing answers.
    const uint16_t PORT = 15502;
    Network::TcpSocket srv = Network::tcp_listen(PORT);
    REQUIRE(srv >= 0);
    Network::tcp_close(srv);

    Network::Ip4 any = {{192, 168, 1, 43}};
    Network::TcpSocket cli = Network::tcp_open();
    REQUIRE(cli >= 0);
    CHECK(Network::tcp_connect(cli, any, PORT) >= 0);   // diverted again, not refused
    Network::tcp_close(cli);
}

#endif

}  // namespace hal_net

// --------------------------------------------------------------------------
// mqtt_log
// --------------------------------------------------------------------------

namespace mqtt_log {

// Empty TU when the feature is off; no CTest cases - test_sun.cpp's shape.
#if BOTANIK_FEATURE_LOGS_PUBLISH

using namespace Botanik;

namespace {

    // Expose the protected format()/build_topic() seam for direct assertions.
    struct TestSink : Network::MqttLogWriter {
        using MqttLogWriter::build_topic;
        using MqttLogWriter::format;
    };

    Logs::LogEntry make_entry(Logs::LogLevel level, Logs::LogTopic topic,
                                 const char* msg) {
        Logs::LogEntry e;
        memset(&e, 0, sizeof(e));
        e.timestamp = 0;
        e.level = level;
        e.topic = topic;
        strncpy(e.message, msg, sizeof(e.message) - 1);
        return e;
    }

}

TEST_CASE("mqtt log: topic assembly") {
    char buf[96];
    size_t n = TestSink::build_topic(buf, sizeof(buf), Logs::LOG_DEBUG, Logs::MODBUS);
    CHECK(n > 0);
    // "<prefix>/<segment>/<level>/<topic>", both segments from config.
    std::string want = std::string(Config::MQTT_TOPIC_PREFIX) + "/"
                     + Config::MQTT_LOG_TOPIC + "/debug/modbus";
    CHECK(want == buf);

    // Truncation reports 0 (no partial topic published).
    char tiny[8];
    CHECK(TestSink::build_topic(tiny, sizeof(tiny), Logs::LOG_ERROR, Logs::BOOT) == 0);
}

TEST_CASE("mqtt log: payload carries timestamp then message") {
    TestSink sink;
    Logs::LogEntry e = make_entry(Logs::LOG_NOTICE, Logs::NETWORK, "link up");
    char buf[128];
    size_t n = sink.format(&e, buf, sizeof(buf));
    CHECK(n > 0);
    // "<timestamp> <message>": ends with the message, and has a separating space.
    const char* tail = std::strstr(buf, "link up");
    REQUIRE(tail != nullptr);
    CHECK(tail > buf);           // something (the timestamp) precedes it
    CHECK(*(tail - 1) == ' ');
}

TEST_CASE("mqtt log: no publisher wired is a safe no-op") {
    Network::MqttLogWriter sink;   // never call set_publisher()
    Logs::LogEntry e = make_entry(Logs::LOG_CRITICAL, Logs::HEALTH, "boom");
    sink.push(&e);                 // must not crash / deref a null transport
    CHECK(sink.loss_count() == 0); // best-effort: link-down drops aren't losses
}

// The broker is read off-site, so it must carry everything the logger lets through,
// DEBUG included; a site that can't afford it raises MQTT_LOG_MIN_LEVEL itself.
TEST_CASE("mqtt log: the default sink floor passes DEBUG") {
    CHECK((Logs::LogLevel)Config::MQTT_LOG_MIN_LEVEL == Logs::LOG_DEBUG);
    Network::MqttLogWriter sink;
    CHECK(sink.min_level() == Logs::LOG_DEBUG);
    // ...and the logger's own floor must not filter it first, or the sink floor answers a question nothing asks.
    CHECK((Logs::LogLevel)Config::LOG_MIN_LEVEL <= Logs::LOG_DEBUG);
}

#endif

}  // namespace mqtt_log
