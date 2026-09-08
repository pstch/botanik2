/**
 * @file test_mqtt.cpp
 * @brief The MqttClient against a real broker socket, under backpressure.
 *
 * The publisher is QoS 0 and the transport is a stream, which puts two rules in
 * tension: a message that will not fit may be dropped, but a message that is
 * half written may not, since the bytes after it are unreadable until the rest
 * goes. A broker that reads slowly trips both if the session ends on the first
 * short write and the reconnect publishes back into the same full socket.
 */

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "doctest.h"

#include "libbotanik.h"
#include "libbotanik/hal/net.h"
#include "libbotanik/network/mqtt.h"
#include "libbotanik/network/network.h"
#include "libbotanik/network/resolver.h"

namespace tests { namespace mqtt_client {

using namespace Botanik;
using namespace Botanik::Network;

#if BOTANIK_FEATURE_MQTT && BOTANIK_FEATURE_NETWORK

namespace {

/** @brief A broker on a real socket, and a client pointed at it.
 *
 *  The listener goes through HAL::Network::tcp_listen so the simulator's HAL
 *  records the port: a connect aimed at a port this process serves reaches it
 *  instead of being diverted to the in-process mock. */
struct BrokerFixture {
    Logs::Logger    log;
    Tasks::Registry reg;
    NameResolver    resolver;
    NetworkHandler  net;
    MqttClient      mqtt;
    uint64_t        now  = 0;
    uint16_t        port = 0;
    HAL::Network::TcpSocket listener = -1;
    int             peer = -1;      ///< the broker's end of the connection

    BrokerFixture()
        : net(SetupContext(reg, &log), &resolver),
          mqtt(SetupContext(reg, &log), &resolver, &net) {
        // A port per fixture: cases in one binary and several binaries under a
        // parallel ctest must not fight over one.
        static uint16_t next = 15883;
        port = next++;
        listener = HAL::Network::tcp_listen(port);
        REQUIRE(listener >= 0);

        net.register_tasks();
        mqtt.register_tasks();
        reg.setup(0);

        NetworkConfig nc;                       // the sim link is always up
        net.set_config(nc);
        pump(4);
        REQUIRE(net.is_up());
    }
    ~BrokerFixture() {
        if (peer >= 0) ::close(peer);
        if (listener >= 0) HAL::Network::tcp_close(listener);
    }

    BrokerFixture(const BrokerFixture&) = delete;
    BrokerFixture& operator=(const BrokerFixture&) = delete;

    void pump(int n = 4, uint32_t step_ms = 10) {
        for (int i = 0; i < n; i++) {
            reg.run_phase(Tasks::PHASE_CONFIG,  now);
            reg.run_phase(Tasks::PHASE_SERVICE, now);
            now += step_ms;
        }
    }

    /** @brief Point the client at the fixture's broker and let it connect. */
    void connect(uint16_t ka_sec = 20) {
        MqttConfig cfg;
        cfg.enabled = true;
        cfg.port    = port;
        cfg.ka_sec  = ka_sec;
        char ab[16];
        HAL::Network::ip4_to_str(HAL::Network::mock_loopback_ip(), ab, sizeof(ab));
        snprintf(cfg.host, sizeof(cfg.host), "%s", ab);
        mqtt.set_config(cfg);
        // Accept and answer until the client says it is up: connect, CONNECT,
        // CONNACK, each one beat apart.
        for (int i = 0; i < 40 && !mqtt.connected(); i++) {
            pump(1);
            serve();
        }
        REQUIRE(mqtt.connected());
    }

    /** @brief Accept the pending connection, then answer whatever arrived:
     *  CONNECT -> CONNACK, PINGREQ -> PINGRESP. Anything else is read and
     *  dropped, as a broker does with a QoS-0 PUBLISH. */
    void serve() {
        if (peer < 0) {
            peer = ::accept(listener, nullptr, nullptr);
            if (peer >= 0) ::fcntl(peer, F_SETFL, O_NONBLOCK);
        }
        if (peer < 0) return;
        uint8_t buf[4096];
        for (;;) {
            const ssize_t n = ::read(peer, buf, sizeof(buf));
            if (n <= 0) break;
            for (ssize_t i = 0; i < n; i++) {
                if ((buf[i] & 0xF0) == 0x10) {
                    const uint8_t ack[4] = { 0x20, 0x02, 0x00, 0x00 };
                    ssize_t w = ::write(peer, ack, sizeof(ack)); (void)w;
                    break;
                }
                if ((buf[i] & 0xF0) == 0xC0 && buf[i] == 0xC0) {
                    const uint8_t pong[2] = { 0xD0, 0x00 };
                    ssize_t w = ::write(peer, pong, sizeof(pong)); (void)w;
                    break;
                }
            }
        }
    }

    /** @brief Publish until one is refused, or give up. Returns the number that
     *  went out; the broker is never read, so the socket fills. */
    int fill(int limit = 100000) {
        char payload[Config::MQTT_PAYLOAD_LEN];
        memset(payload, 'x', sizeof(payload) - 1);
        payload[sizeof(payload) - 1] = '\0';
        for (int i = 0; i < limit; i++)
            if (!mqtt.publish("t/fill", payload, false)) return i;
        return limit;
    }
};

}  // namespace

TEST_CASE("mqtt: a socket with no room drops the message, not the session") {
    BrokerFixture f;
    f.connect();

    // Nothing reads the broker end, so the kernel buffers fill and a publish is
    // eventually refused. That is backpressure, and the session survives it.
    const int sent = f.fill();
    INFO("publishes accepted before the socket filled: " << sent);
    REQUIRE(sent > 0);
    REQUIRE(sent < 100000);
    CHECK(f.mqtt.connected());
}

TEST_CASE("mqtt: it keeps publishing once the broker catches up") {
    BrokerFixture f;
    f.connect();
    REQUIRE(f.fill() > 0);

    // The broker drains; the client finishes any half-written packet and takes
    // messages again, on the same session it never dropped.
    for (int i = 0; i < 20; i++) { f.serve(); f.pump(1); }
    CHECK(f.mqtt.connected());
    CHECK(f.mqtt.publish("t/after", "1", false));
}

TEST_CASE("mqtt: a broker that stops reading for good ends the session") {
    BrokerFixture f;
    f.connect();
    REQUIRE(f.fill() > 0);

    // Never served again: the keep-alive gets no PINGRESP and the stall timer
    // runs out on whatever tail is left. Either way it must not sit "ready"
    // over a socket nothing is reading.
    for (int i = 0; i < 2000 && f.mqtt.connected(); i++) f.pump(1, 100);
    CHECK_FALSE(f.mqtt.connected());
    // ...and it says which of the two it was, so the status page and the log
    // name the fault rather than reporting "error".
    const std::string why = f.mqtt.fault_str();
    INFO("fault: " << why);
    CHECK((why == "broker silent" || why == "send stalled"));
}

TEST_CASE("mqtt: an unconfigured broker is a fault with a name") {
    BrokerFixture f;
    MqttConfig cfg;
    cfg.enabled = true;             // host left empty
    f.mqtt.set_config(cfg);
    f.pump(4);

    CHECK_FALSE(f.mqtt.connected());
    CHECK(std::string(f.mqtt.fault_str()) == "no broker");
    CHECK(std::string(f.mqtt.status_str()) == "no broker");
}

#endif  // BOTANIK_FEATURE_MQTT && BOTANIK_FEATURE_NETWORK

}}  // namespace tests::mqtt_client
