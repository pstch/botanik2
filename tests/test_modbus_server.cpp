/**
 * @file test_modbus_server.cpp
 * @brief The Modbus TCP server, spoken to over a real socket.
 *
 * This is the one part of the firmware a stranger on the plant network can
 * reach unauthenticated, so what it does with input nobody vetted is the point:
 * a frame that is not Modbus at all, a request for a register that is not
 * published, a write to a row that is only a reading. The MBAP codec and the
 * register map have their own cases (test_modbus_tcp.cpp, test_modbus_map.cpp);
 * this drives the socket layer over both, as a client would.
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <string>
#include <vector>

#include "doctest.h"

#include "libbotanik.h"
#include "libbotanik/hal/net.h"
#include "libbotanik/modbus/tcp/master.h"
#include "libbotanik/modbus/tcp/slave.h"

#include "site_bank.h"

namespace modbus_server {

using namespace Botanik;
using namespace Botanik::Modbus;

#if BOTANIK_FEATURE_MODBUS_TCP_SERVER && BOTANIK_FEATURE_NETWORK && BOTANIK_FEATURE_VARS

/** @brief A listening TcpSlave, and a socket to talk to it with. */
struct ServerFixture {
    Vars::Bank      bank;
    Logs::Logger    log;
    Tasks::Registry reg;
    TcpSlave        slave;
    uint64_t        now = 0;
    uint16_t        port;

    explicit ServerFixture(bool writable = true)
        : slave(SetupContext(reg, &log), &bank, nullptr) {
        TestSite::build(bank);
        // A port per fixture, so cases inside one binary and several binaries
        // under a parallel ctest do not fight. The listener binds this
        // instance's own 127/8 address, which is the other half of that.
        static uint16_t next = 15502;
        port = next++;
        slave.setup(0);
        TcpSlaveConfig cfg;
        cfg.enabled = true; cfg.writable = writable; cfg.port = port;
        slave.set_config(cfg);
        slave.register_tasks();
        reg.setup(0);
        pump(4);
    }
    ~ServerFixture() { for (size_t i = 0; i < socks.size(); i++) ::close(socks[i]); }

    ServerFixture(const ServerFixture&) = delete;
    ServerFixture& operator=(const ServerFixture&) = delete;

    std::vector<int> socks;

    /** @brief Run the server's beat @p n times. */
    void pump(int n = 4, uint32_t step_ms = 10) {
        for (int i = 0; i < n; i++) {
            reg.run_phase(Tasks::PHASE_INPUT,   now);
            reg.run_phase(Tasks::PHASE_SERVICE, now);
            reg.run_phase(Tasks::PHASE_PRESENT, now);
            now += step_ms;
        }
    }

    /** @brief Connect a client. -1 if the server is not listening. */
    int connect_client() {
        HAL::Network::Ip4 ip = HAL::Network::mock_loopback_ip();
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(fd >= 0);
        sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port   = htons(port);
        memcpy(&sin.sin_addr, ip.b, 4);
        if (::connect(fd, (sockaddr*)&sin, sizeof(sin)) < 0) { ::close(fd); return -1; }
        ::fcntl(fd, F_SETFL, O_NONBLOCK);
        socks.push_back(fd);
        pump(4);
        return fd;
    }

    /** @brief Send @p f, beat the server, and read whatever came back. */
    std::vector<uint8_t> exchange(int fd, const std::vector<uint8_t>& f) {
        ssize_t w = ::write(fd, f.data(), f.size()); (void)w;
        pump(8);
        std::vector<uint8_t> out;
        uint8_t buf[1024];
        for (int i = 0; i < 8; i++) {
            const ssize_t n = ::read(fd, buf, sizeof(buf));
            if (n > 0) out.insert(out.end(), buf, buf + n);
            else if (out.size()) break;
            pump(2);
        }
        return out;
    }
    /** @brief Whether the peer closed on us. */
    bool closed(int fd) {
        uint8_t b;
        const ssize_t n = ::read(fd, &b, 1);
        return n == 0;
    }
};

/** @brief An MBAP frame around @p pdu. */
static std::vector<uint8_t> mbap(uint16_t txn, uint8_t unit,
                                 const std::vector<uint8_t>& pdu, int proto = 0) {
    const uint16_t len = (uint16_t)(pdu.size() + 1);
    std::vector<uint8_t> f = {
        (uint8_t)(txn >> 8), (uint8_t)txn,
        (uint8_t)(proto >> 8), (uint8_t)proto,
        (uint8_t)(len >> 8), (uint8_t)len, unit };
    f.insert(f.end(), pdu.begin(), pdu.end());
    return f;
}
static std::vector<uint8_t> read_pdu(uint8_t fc, uint16_t addr, uint16_t qty) {
    return { fc, (uint8_t)(addr >> 8), (uint8_t)addr,
                 (uint8_t)(qty >> 8),  (uint8_t)qty };
}

TEST_CASE("modbus server: it binds its port and reports it") {
    ServerFixture f;
    CHECK(f.slave.listening());
    const SlaveContext c = f.slave.context();
    CHECK(c.listening);
    CHECK(c.port == f.port);
    CHECK(c.rows > 0);
}

TEST_CASE("modbus server: a client connects and is counted") {
    ServerFixture f;
    const int fd = f.connect_client();
    REQUIRE(fd >= 0);
    CHECK(f.slave.context().clients == 1);
}

TEST_CASE("modbus server: a read of a published register is answered") {
    ServerFixture f;
    const int fd = f.connect_client();
    REQUIRE(fd >= 0);

    // Register addresses are twice the row number; row 900 upward is the
    // library's own block, which every site publishes.
    const std::vector<uint8_t> reply =
        f.exchange(fd, mbap(0x0042, 1, read_pdu(FC_READ_HOLDING, 900 * 2, 2)));
    REQUIRE(reply.size() >= 9);
    CHECK(reply[0] == 0x00);          // the transaction id, echoed
    CHECK(reply[1] == 0x42);
    CHECK(reply[2] == 0x00);          // protocol id
    CHECK(reply[3] == 0x00);
    CHECK(reply[7] == FC_READ_HOLDING);
    CHECK(reply[8] == 4);             // byte count: two registers
    CHECK(reply.size() == 9 + 4u);
}

TEST_CASE("modbus server: a read nothing publishes is refused, and the link lives") {
    ServerFixture f;
    const int fd = f.connect_client();
    REQUIRE(fd >= 0);

    // A content error: the framing was fine, so one exception goes back and the
    // connection stays. A scanner walking the address space must not cost a
    // real client its session.
    const std::vector<uint8_t> reply =
        f.exchange(fd, mbap(1, 1, read_pdu(FC_READ_HOLDING, 0xFFF0, 2)));
    REQUIRE(reply.size() == 9);
    CHECK(reply[7] == (FC_READ_HOLDING | 0x80));
    CHECK(reply[8] == EX_ILLEGAL_ADDRESS);

    INFO("...and the same client is still served");
    const std::vector<uint8_t> ok =
        f.exchange(fd, mbap(2, 1, read_pdu(FC_READ_HOLDING, 900 * 2, 2)));
    REQUIRE(ok.size() >= 9);
    CHECK(ok[7] == FC_READ_HOLDING);
}

TEST_CASE("modbus server: an unknown function is refused without dropping the peer") {
    ServerFixture f;
    const int fd = f.connect_client();
    REQUIRE(fd >= 0);
    const std::vector<uint8_t> reply =
        f.exchange(fd, mbap(3, 1, { 0x41, 0x00, 0x00, 0x00, 0x01 }));
    REQUIRE(reply.size() == 9);
    CHECK(reply[7] == (0x41 | 0x80));
    CHECK(reply[8] == EX_ILLEGAL_FUNCTION);
    CHECK(f.slave.context().clients == 1);
}

TEST_CASE("modbus server: something that is not Modbus TCP loses the connection") {
    ServerFixture f;
    const int fd = f.connect_client();
    REQUIRE(fd >= 0);

    // Prove the link works first, or "no answer" below would pass on a fixture
    // that never connected at all.
    REQUIRE(f.exchange(fd, mbap(1, 1, read_pdu(FC_READ_HOLDING, 900 * 2, 1))).size() >= 9);
    REQUIRE(f.slave.context().clients == 1);

    // A framing error: TCP is a stream and there is no frame edge to
    // resynchronise to, so the only correct answer is to drop it. Whatever is
    // on this port is not speaking the protocol.
    const std::vector<uint8_t> reply =
        f.exchange(fd, mbap(2, 1, read_pdu(FC_READ_HOLDING, 0, 1), /*proto=*/7));
    CHECK(reply.empty());
    f.pump(8);
    INFO("the server must let go rather than keep reading a stream it has lost");
    CHECK(f.slave.context().clients == 0);
}

TEST_CASE("modbus server: a half frame is waited for, not answered") {
    ServerFixture f;
    const int fd = f.connect_client();
    REQUIRE(fd >= 0);

    const std::vector<uint8_t> whole = mbap(9, 1, read_pdu(FC_READ_HOLDING, 900 * 2, 1));
    const std::vector<uint8_t> head(whole.begin(), whole.begin() + 5);
    ssize_t w = ::write(fd, head.data(), head.size()); (void)w;
    f.pump(8);
    uint8_t buf[64];
    CHECK(::read(fd, buf, sizeof(buf)) < 0);      // nothing yet
    CHECK(f.slave.context().clients == 1);        // and still connected

    const std::vector<uint8_t> tail(whole.begin() + 5, whole.end());
    const std::vector<uint8_t> reply = f.exchange(fd, tail);
    REQUIRE(reply.size() >= 9);
    CHECK(reply[7] == FC_READ_HOLDING);
}

TEST_CASE("modbus server: a write is refused outright when the map is read-only") {
    ServerFixture f(/*writable=*/false);
    const int fd = f.connect_client();
    REQUIRE(fd >= 0);
    REQUIRE_FALSE(f.slave.writable());

    // mb_write_enable off is the operator saying the plant network may look and
    // not touch. It must be answered as a refusal, not silently ignored.
    const std::vector<uint8_t> reply =
        f.exchange(fd, mbap(4, 1, { FC_WRITE_REGISTER, 0x07, 0x08, 0x00, 0x01 }));
    REQUIRE(reply.size() == 9);
    CHECK(reply[7] == (FC_WRITE_REGISTER | 0x80));
    CHECK(f.slave.context().clients == 1);
}

TEST_CASE("modbus server: two clients are served independently") {
    ServerFixture f;
    const int a = f.connect_client();
    const int b = f.connect_client();
    REQUIRE(a >= 0);
    REQUIRE(b >= 0);
    CHECK(f.slave.context().clients == 2);

    const std::vector<uint8_t> ra =
        f.exchange(a, mbap(0x1111, 1, read_pdu(FC_READ_HOLDING, 900 * 2, 1)));
    const std::vector<uint8_t> rb =
        f.exchange(b, mbap(0x2222, 1, read_pdu(FC_READ_HOLDING, 900 * 2, 1)));
    REQUIRE(ra.size() >= 9);
    REQUIRE(rb.size() >= 9);
    // Each gets its OWN transaction id back: crossing them would have a master
    // match an answer to the wrong question.
    CHECK(((ra[0] << 8) | ra[1]) == 0x1111);
    CHECK(((rb[0] << 8) | rb[1]) == 0x2222);
}

TEST_CASE("modbus server: disabling it gives the port back") {
    ServerFixture f;
    REQUIRE(f.slave.listening());
    TcpSlaveConfig cfg;
    cfg.enabled = false; cfg.port = f.port;
    f.slave.set_config(cfg);
    f.pump(4);
    CHECK_FALSE(f.slave.listening());
    CHECK_FALSE(f.slave.context().listening);
}


#if BOTANIK_FEATURE_MODBUS_TCP_CLIENT
/** @brief The firmware's own TcpMaster pointed at the server above: the pair
 *  the simulator uses to check the register map against itself. */
struct PairFixture : ServerFixture {
    TcpMaster master;
    explicit PairFixture() : master(SetupContext(reg, &log)) {
        // TcpMaster registers no tasks: it is driven by whoever owns it, the
        // way the sim's own mbcheck does. So each beat here steps it by hand
        // alongside the server's registered halves.
        master.setup(0);
        master.connect_to(HAL::Network::mock_loopback_ip(), port);
        beat(40);
    }

    /** @brief @p n beats of the server AND the master. */
    void beat(int n = 1) {
        for (int i = 0; i < n; i++) {
            pump(1);
            master.step(now, Tasks::PHASE_SERVICE);
        }
    }
    /** @brief Beat both ends until the master finishes, or give up. */
    bool settle(int rounds = 400) {
        for (int i = 0; i < rounds; i++) {
            beat(1);
            if (master.done()) return true;
        }
        return false;
    }
};

TEST_CASE("modbus tcp pair: the master reads what the server publishes") {
    PairFixture f;
    REQUIRE(f.master.connected());
    CHECK(f.slave.context().clients == 1);

    REQUIRE(f.master.read(1, FC_READ_HOLDING, 900 * 2, 2));
    REQUIRE(f.settle());
    CHECK(f.master.error() == ERR_OK);
    CHECK(f.master.response().len == 4);     // two registers
    CHECK(f.master.txns() == 1);
    f.master.take_result();
}

TEST_CASE("modbus tcp pair: a refusal reaches the master as a refusal") {
    PairFixture f;
    REQUIRE(f.master.connected());

    // The slave answers, and declines. That is a RESULT - the device is alive
    // and the request was wrong - not a comms fault, and the master must not
    // count it as one.
    REQUIRE(f.master.read(1, FC_READ_HOLDING, 0xFFF0, 2));
    REQUIRE(f.settle());
    CHECK(f.master.error() == ERR_EXCEPTION);
    CHECK(f.master.response().ex == EX_ILLEGAL_ADDRESS);
    CHECK(f.master.faults() == 0);
    f.master.take_result();

    INFO("...and the session survives it");
    REQUIRE(f.master.read(1, FC_READ_HOLDING, 900 * 2, 1));
    REQUIRE(f.settle());
    CHECK(f.master.error() == ERR_OK);
}

TEST_CASE("modbus tcp pair: only one request is in flight, and there is no queue") {
    PairFixture f;
    REQUIRE(f.master.connected());
    REQUIRE(f.master.read(1, FC_READ_HOLDING, 900 * 2, 1));
    // A second must be REFUSED rather than silently enqueued: a caller that
    // believed it was queued would wait for an answer that never comes.
    CHECK_FALSE(f.master.read(1, FC_READ_HOLDING, 900 * 2, 1));
    REQUIRE(f.settle());
    f.master.take_result();
    CHECK(f.master.read(1, FC_READ_HOLDING, 900 * 2, 1));
}

TEST_CASE("modbus tcp pair: a function code that is not a read is not framed") {
    PairFixture f;
    REQUIRE(f.master.connected());
    // Refused before a byte goes out, so a caller's mistake is not the slave's
    // problem.
    CHECK_FALSE(f.master.read(1, FC_WRITE_COIL, 900 * 2, 1));
    CHECK_FALSE(f.master.read(1, 0x41, 900 * 2, 1));
}

TEST_CASE("modbus tcp pair: the server going away is a fault, not a hang") {
    PairFixture f;
    REQUIRE(f.master.connected());

    TcpSlaveConfig off;
    off.enabled = false; off.port = f.port;
    f.slave.set_config(off);
    f.beat(4);
    REQUIRE_FALSE(f.slave.listening());

    // Whatever happens, it must reach a terminal state rather than sit in
    // flight for ever: a master with no timeout is a task that never completes.
    f.master.read(1, FC_READ_HOLDING, 900 * 2, 1);
    const bool finished = f.settle(400);
    INFO("done=" << f.master.done() << " connected=" << f.master.connected()
         << " err=" << (int)f.master.error());
    CHECK((finished || !f.master.connected()));
}
#endif

#endif

}  // namespace modbus_server
