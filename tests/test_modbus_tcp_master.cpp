/**
 * @file test_modbus_tcp_master.cpp
 * @brief The TCP master's state machine, against a real slave in the same
 *        process.
 *
 * The simulator's HAL diverts every address to an in-process loopback, so a
 * master and a slave built here talk to each other over real sockets without
 * touching the host network. That matters: the interesting cases are the ones
 * with no natural end - a peer that accepts and never reads, a frame that leaves
 * the stream out of step - and both used to be states the machine could sit in
 * for ever.
 */
#include <cstdint>
#include <cstring>

#include "doctest.h"

#include "libbotanik_site.h"
#include "libbotanik.h"
#include "libbotanik/modbus/tcp/master.h"   // not in modbus.h's aggregate
#include "libbotanik/modbus/tcp/slave.h"

#include BOTANIK_SITE_HEADER

#if BOTANIK_FEATURE_MODBUS_TCP_CLIENT && BOTANIK_FEATURE_MODBUS_TCP_SERVER

using namespace Botanik;
using namespace Botanik::Vars;
using namespace Botanik::Modbus;

namespace {

/** @brief A slave and a master, wired to each other through the sim's loopback. */
struct Pair {
    Bank      bank;
    TcpSlave  slave;
    TcpMaster master;
    uint64_t  now = 1000;

    Pair() : bank(Site::g_site.vars), slave(&bank), master() {
        bank.init();
        slave.setup(now);
        master.setup(now);
    }

    /** @brief One beat of both ends, in the order the firmware runs them. */
    void beat() {
        now += 10;
        slave.step(now);
        master.step(now);
        slave.service_tx(now);
    }

    /** @brief Beat until @p done, or give up after @p budget ms. */
    template <typename F>
    bool until(F done, uint32_t budget) {
        const uint64_t started = now;
        while (!done() && (now - started) < budget) beat();
        return done();
    }

    /** @brief The address of the lowest published row: the map is sparse and
     *  starts well above zero, so a read of register 0 is off the end of it. */
    uint16_t first_addr() const {
        const VarTables& t = Site::g_site.vars;
        uint16_t best = 0xFFFF;
        for (uint16_t i = 0; i < t.count; ++i) {
            const Variable& d = t.var[i];
            if (!Map::published(d) || d.type == T_BOOL) continue;
            if (d.bind.reg < best) best = d.bind.reg;
        }
        return (uint16_t)(best * 2);
    }

    bool connect() {
        master.connect_to(HAL::Network::mock_loopback_ip(), Config::MODBUS_TCP_PORT);
        TcpMaster* m = &master;
        return until([m] { return m->connected(); }, 5000);
    }
};

}  // namespace

TEST_CASE("modbus tcp master: it reaches the slave and reads a register") {
    Pair p;
    REQUIRE(p.slave.listening());
    REQUIRE(p.connect());

    REQUIRE(p.master.read(Config::MODBUS_TCP_UNIT_ID, FC_READ_HOLDING,
                          p.first_addr(), 2));

    TcpMaster* m = &p.master;
    REQUIRE(p.until([m] { return m->done(); }, 5000));
    INFO("error " << (int)p.master.error());
    // A real answer, not merely a verdict: this is the end-to-end path, so a
    // timeout here would mean the two never spoke and every other case in this
    // file was passing on the strength of its deadlines alone.
    REQUIRE(p.master.error() == ERR_OK);
    CHECK(p.master.response().len == 4);
}

TEST_CASE("modbus tcp master: a function code that is not a read is refused") {
    // read() took over from a read_bits() that framed whatever fc it was given,
    // so a write code could go out through the read path carrying a read's
    // Expect - and then be checked against the wrong shape of answer.
    Pair p;
    REQUIRE(p.connect());

    CHECK_FALSE(p.master.read(1, FC_WRITE_REGISTERS, 0, 2));
    CHECK_FALSE(p.master.read(1, FC_WRITE_COIL, 0, 1));
    CHECK(p.master.read(1, FC_READ_HOLDING, 0, 2));
}

TEST_CASE("modbus tcp master: a transaction always reaches a verdict") {
    // Every state has a deadline now, MA_SENDING included - it was the one with
    // none, so a peer that accepted and never read wedged the master for good.
    Pair p;
    REQUIRE(p.connect());
    REQUIRE(p.master.read(Config::MODBUS_TCP_UNIT_ID, FC_READ_HOLDING,
                          p.first_addr(), 2));

    // Well past every deadline the master has.
    const uint32_t budget = (uint32_t)(Config::MODBUS_TCP_RESPONSE_TIMEOUT_MS * 4u
                                     + Config::MODBUS_TCP_CONNECT_TIMEOUT_MS * 4u);
    TcpMaster* m = &p.master;
    CHECK(p.until([m] { return m->done(); }, budget));
}

TEST_CASE("modbus tcp master: it can be driven again after a completed transaction") {
    // A transaction that ends must leave the machine usable, whatever it ended
    // as - otherwise one bad answer takes the link down until a reconnect.
    Pair p;
    REQUIRE(p.connect());

    for (int round = 0; round < 3; ++round) {
        if (!p.master.connected()) {
            REQUIRE(p.connect());
        }
        REQUIRE(p.master.read(Config::MODBUS_TCP_UNIT_ID, FC_READ_HOLDING,
                              (uint16_t)(p.first_addr() + round * 2), 2));
        TcpMaster* m = &p.master;
        REQUIRE(p.until([m] { return m->done(); }, 5000));
        (void)p.master.take_result();
    }
}

#endif // BOTANIK_FEATURE_MODBUS_TCP_CLIENT && BOTANIK_FEATURE_MODBUS_TCP_SERVER
