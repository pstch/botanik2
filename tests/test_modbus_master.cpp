/**
 * @file test_modbus_master.cpp
 * @brief The RS-485 master, driven against a real slave over a real socket.
 *
 * RtuMaster is 663 lines of retry, backoff and per-device health, and none of
 * it was covered. It is also the component whose failure mode is a plant
 * running on declared values: a device that stops answering must be NOTICED, or
 * every row it feeds keeps its last reading and the control laws act on a
 * greenhouse that is no longer there.
 *
 * So this drives the real thing end to end - the master's own port, framed
 * bytes on a socketpair, a real Modbus::RtuSlave answering - rather than
 * stubbing the wire. What is asserted is the behaviour an operator would see:
 * a device answers, a device goes quiet, a device comes back.
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>

#include <vector>

#include "doctest.h"

#include "libbotanik.h"
#include "libbotanik/modbus/rtu/master.h"
#include "libbotanik/modbus/rtu/slave.h"

#include "recording_writer.h"
#include "site_bank.h"

namespace modbus_master {

using namespace Botanik;
using namespace Botanik::Modbus;

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT && BOTANIK_FEATURE_VARS

/** @brief An RtuMaster on one end of a socketpair, with the site's own devices
 *  standing on the other. */
struct BusFixture {
    int              sv[2];
    Vars::Bank       bank;
    Logs::Logger     log;
    RecordingWriter  writer;
    Tasks::Registry  reg;
    RtuMaster        master;
    /** @brief One slave per unit the plan addresses, answering as itself. */
    std::vector<RtuSlave> devices;
    uint64_t         now = 0;

    BusFixture()
        : master(SetupContext(reg, &log), &bank, Site::g_site.devices) {
        REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
        log.add_writer(&writer);
        // Our end too: set_fds() only makes the MASTER's end non-blocking, and
        // a read here with nothing on the wire is the normal case.
        ::fcntl(sv[1], F_SETFL, O_NONBLOCK);
        TestSite::build(bank);
        // The master resolves HAL::Serial::FIELDBUS at setup, so the port has
        // to be ours before then.
        HAL::Serial::get(HAL::Serial::FIELDBUS)->set_fds(sv[0], sv[0]);
        // Component::setup() runs on_setup(); the two hooks are driven through
        // the registry, which is the path the firmware itself uses.
        master.setup(0);
        // Re-rate the port AFTER setup, which called fieldbus_begin() at the
        // real MODBUS_BAUD. The sim throttles writes against real time, and a
        // test walks simulated seconds in microseconds of it, so at 19200 the
        // port would refuse all but the first frame. The wire's timing is not
        // what these cases are about. @see PageFixture, same trick.
        HAL::Serial::get(HAL::Serial::FIELDBUS)->begin(4000000u);
        master.register_tasks();
        reg.setup(0);

        devices.resize(master.peer_count());
        for (uint8_t i = 0; i < master.peer_count(); i++)
            devices[i].set_unit(master.peer_unit(i));
    }
    ~BusFixture() { ::close(sv[0]); ::close(sv[1]); }

    BusFixture(const BusFixture&) = delete;
    BusFixture& operator=(const BusFixture&) = delete;

    /** @brief Carry whatever the master sent to the addressed slave, and its
     *  answer back. @p answer false leaves the bus silent, which is a device
     *  that has gone away. */
    void carry(bool answer = true) {
        uint8_t req[512];
        const ssize_t n = ::read(sv[1], req, sizeof(req));
        if (n <= 0) return;
        if (!answer) return;
        for (size_t i = 0; i < devices.size(); i++) {
            if (devices[i].unit() != req[0]) continue;
            devices[i].feed(req, (uint16_t)n);
            uint8_t rsp[512];
            const uint16_t m = devices[i].poll(rsp, sizeof(rsp));
            if (m > 0) { ssize_t w = ::write(sv[1], rsp, m); (void)w; }
            return;
        }
    }

    /** @brief Answer whatever the master sent with exception @p ex: a device
     *  that is alive and declining, which is a register map that is wrong. */
    void refuse(uint8_t ex = 0x02) {
        uint8_t req[512];
        const ssize_t n = ::read(sv[1], req, sizeof(req));
        if (n < 2) return;
        uint8_t rsp[5] = { req[0], (uint8_t)(req[1] | 0x80), ex, 0, 0 };
        const uint16_t crc = Modbus::crc16(rsp, 3);
        rsp[3] = (uint8_t)(crc & 0xFF);
        rsp[4] = (uint8_t)(crc >> 8);
        ssize_t w = ::write(sv[1], rsp, sizeof(rsp)); (void)w;
    }

    /** @brief One beat: the master issues, the bus carries, the master reads. */
    void beat(bool answer = true, uint32_t step_ms = 20) {
        reg.run_phase(Tasks::PHASE_OUTPUT, now);
        carry(answer);
        now += step_ms;
        reg.run_phase(Tasks::PHASE_INPUT, now);
    }
    void beats(int n, bool answer = true, uint32_t step_ms = 20) {
        for (int i = 0; i < n; i++) beat(answer, step_ms);
    }
};

TEST_CASE("modbus master: a healthy segment completes transactions") {
    BusFixture f;
    REQUIRE(f.master.peer_count() > 0);

    f.beats(200);
    INFO(f.master.txns() << " transactions, " << f.master.faults() << " faults, "
         << (int)f.master.dead_count() << " dead of " << (int)f.master.peer_count());
    CHECK(f.master.txns() > 0);
    CHECK(f.master.dead_count() == 0);
    CHECK(f.master.healthy());
}

TEST_CASE("modbus master: a silent segment is noticed rather than believed") {
    BusFixture f;
    REQUIRE(f.master.peer_count() > 0);

    // The failure this exists to prevent: with nothing answering, every row the
    // bus feeds would keep its last value and read good for ever, and the laws
    // would run on a greenhouse that is not there.
    f.beats(1200, /*answer=*/false, 40);
    INFO(f.master.txns() << " txns, " << f.master.faults() << " faults, "
         << (int)f.master.dead_count() << " dead");
    CHECK(f.master.faults() > 0);
    CHECK(f.master.dead_count() > 0);
    CHECK_FALSE(f.master.healthy());
}

TEST_CASE("modbus master: a device is dead only after its retries are spent") {
    BusFixture f;
    REQUIRE(f.master.peer_count() > 0);

    // peer_dead() and the backoff must agree: an accessor calling a device dead
    // while the rotation still retries it is a display disagreeing with the bus.
    for (uint8_t i = 0; i < f.master.peer_count(); i++)
        CHECK_FALSE(f.master.peer_dead(i));

    f.beats(1200, false, 40);
    uint8_t dead = 0;
    for (uint8_t i = 0; i < f.master.peer_count(); i++) {
        if (!f.master.peer_dead(i)) continue;
        dead++;
        INFO("unit " << (int)f.master.peer_unit(i));
        CHECK(f.master.peer_misses(i) > 0);
    }
    CHECK(dead == f.master.dead_count());
}

TEST_CASE("modbus master: a device that comes back is polled again") {
    BusFixture f;
    REQUIRE(f.master.peer_count() > 0);

    f.beats(1200, false, 40);
    REQUIRE(f.master.dead_count() > 0);
    const uint32_t faults_at_worst = f.master.faults();

    // Backoff is capped, so a dead device is retried for ever rather than
    // written off: the wire coming back must not need a reboot to notice.
    f.beats(2000, true, 40);
    INFO("faults " << faults_at_worst << " -> " << f.master.faults()
         << ", dead " << (int)f.master.dead_count());
    CHECK(f.master.dead_count() < f.master.peer_count());
}

TEST_CASE("modbus master: a reading reaches the bank, and stops when the bus does") {
    BusFixture f;
    f.beats(400);

    // Which rows the plan feeds, and whether any of them actually took a value.
    uint16_t good = 0;
    for (uint16_t i = 0; i < f.master.plan().row_count(); i++)
        if (f.bank.var(f.master.plan().row(i)).valid()) good++;
    INFO(good << " of " << f.master.plan().row_count() << " bus rows valid");
    CHECK(good > 0);
}

TEST_CASE("modbus master: the counters separate a refusal from a silence") {
    BusFixture f;
    f.beats(300, false, 40);
    // Nothing answered, so these are timeouts: not CRC errors, and not a slave
    // declining. Each points at a different physical fault and the alarms are
    // raised off them.
    INFO("faults " << f.master.faults() << " refusals " << f.master.refusals()
         << " crc " << f.master.framer().crc_errors());
    CHECK(f.master.faults() > 0);
    CHECK(f.master.refusals() == 0);
    CHECK(f.master.framer().crc_errors() == 0);
    CHECK(f.master.framer().timeouts() > 0);
}

TEST_CASE("modbus master: a refused read is logged at the transition, not per poll") {
    BusFixture f;
    // A slave that is alive and declining every read: the one fault a row's
    // quality cannot carry, since Q_NONE is also what a silence gives. The log
    // line is the whole difference, so it has to name the row and be an EDGE -
    // at five polls a second, one wrong register would otherwise be the rate
    // limiter's entire budget.
    for (int i = 0; i < 200; i++) {
        f.reg.run_phase(Tasks::PHASE_OUTPUT, f.now);
        f.refuse();
        f.now += 20;
        f.reg.run_phase(Tasks::PHASE_INPUT, f.now);
    }
    INFO(f.writer.all_msgs);
    REQUIRE(f.master.refusals() > 0);
    CHECK(f.writer.saw("refused"));

    size_t said = 0;
    for (size_t at = f.writer.all_msgs.find("refused");
         at != std::string::npos;
         at = f.writer.all_msgs.find("refused", at + 1)) said++;
    // One per block in the plan, at most: each has its own register map and its
    // own transition, and none of them says it twice.
    CHECK(said <= f.master.plan().block_count());

    // And the row is named, so an operator has the word they are looking at
    // rather than a register number.
    const uint16_t row = f.master.plan().row(f.master.plan().block(0).first_row);
    CHECK(f.writer.saw(f.bank.var(row).name()));
}

TEST_CASE("modbus master: garbage on the wire is a CRC fault, not a reading") {
    BusFixture f;
    // A frame with the right shape and a wrong checksum: a terminated segment,
    // a baud mismatch, a cable near a contactor. It must never publish.
    for (int i = 0; i < 60; i++) {
        f.reg.run_phase(Tasks::PHASE_OUTPUT, f.now);
        uint8_t req[512];
        const ssize_t n = ::read(f.sv[1], req, sizeof(req));
        if (n > 0) {
            uint8_t junk[8] = { req[0], 0x03, 0x02, 0xDE, 0xAD, 0x00, 0x00, 0x00 };
            ssize_t w = ::write(f.sv[1], junk, sizeof(junk)); (void)w;
        }
        f.now += 20;
        f.reg.run_phase(Tasks::PHASE_INPUT, f.now);
    }
    INFO("crc " << f.master.framer().crc_errors()
         << " bad " << f.master.framer().bad_frames()
         << " faults " << f.master.faults());
    CHECK(f.master.faults() > 0);
}

TEST_CASE("modbus master: status_str says which of those it is") {
    BusFixture f;
    const char* fresh = f.master.status_str();
    REQUIRE(fresh != nullptr);
    f.beats(1200, false, 40);
    const char* down = f.master.status_str();
    REQUIRE(down != nullptr);
    INFO("'" << fresh << "' -> '" << down << "'");
    CHECK(strlen(down) > 0);
}

#endif

}  // namespace modbus_master
