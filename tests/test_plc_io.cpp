/**
 * @file test_plc_io.cpp
 * @brief The program-facing view (plc/io.h): what a control program can reach,
 *        and what it deliberately cannot.
 *
 * Most of the value of Io is in what it does NOT have, and a missing method is
 * not something a runtime test can assert - deleting one would stop this file
 * compiling rather than fail it. So what is checked here is the behaviour of the
 * surface that remains: that reads go through the same override-aware path every
 * other reader uses, and that a write lands on the row and is owed to the field.
 * The narrowing itself is guarded by the compiler.
 */
#include <cstdint>
#include <cstdio>                   // snprintf - the test log sink
#include <string>

#include "doctest.h"

#include "libbotanik_site.h"
#include "libbotanik.h"
#include "libbotanik/plc/io.h"

#include BOTANIK_SITE_HEADER

using namespace Botanik;
using namespace Botanik::Vars;
using Botanik::Plc::Io;

namespace {

/** @brief A bank over the site's real tables, running. */
struct Live {
    Bank bank;
    Live() : bank(Site::g_site.vars) {
        bank.init();
        bank.enable_control(Bank::INHIBIT_HEALTH);
        bank.enable_control(Bank::INHIBIT_OPERATOR);
    }
    VarRef ref(const char* name) { return bank.find(name); }
};

/** @brief A sink that counts criticals and keeps the last, so a test can ask
 *  whether the driver said something and how often. */
class Criticals : public Logs::LogWriter {
public:
    int  seen = 0;
    char last[Config::LOG_MESSAGE_LEN] = {};

    void push(const Logs::LogEntry* e) override {
        if (e == nullptr || e->level != Logs::LOG_CRITICAL) return;
        seen++;
        std::snprintf(last, sizeof last, "%s", e->message);
    }
    void drain() override {}
protected:
    size_t write(const char*, size_t) override { return 0; }
    size_t format(const Logs::LogEntry*, char*, size_t) override { return 0; }
};

/** @brief A program that drives one named row every beat. Two of them pointed
 *  at the same row is the design defect WP-3 exists to notice. */
class Drives : public Plc::Program {
    VarRef      _r = { VAR_NONE };
    const char* _row;
public:
    Drives(const char* name, const char* row) : Program(name, 0), _row(row) {}
protected:
    void bind(Bank& io) override { _r = io.find(_row); }
    void run(Plc::Context& c) override { c.io.set(_r, 1.0f); }
};

}  // namespace

TEST_CASE("plc io: it stays small enough to pass by value") {
    // The Context is built per activation, so this is per program per beat. A
    // pointer plus the writer byte: one word on the M7, where the byte lands in
    // padding that was already there.
    CHECK(sizeof(Io) <= 2 * sizeof(void*));
}

TEST_CASE("plc io: a program reads what the operator forced, not what the wire said") {
    // The whole point of applying an override in Var::raw(): a forced reading
    // has to reach control logic, or forcing one proves nothing about the logic
    // that watches it.
    Live f;
    Io io(f.bank);
    const VarRef r = f.ref("cli_temp_ext1");
    REQUIRE(f.bank.var(r).meta()->name != nullptr);

    f.bank.var(r).publish(from_f32(T_F32, 20.0f), 1000);
    CHECK(io.value(r) == doctest::Approx(20.0f));
    CHECK(io.valid(r));

    f.bank.override_set(f.bank.var(r), from_f32(T_F32, 55.0f), true);
    INFO("the program must see the forced value");
    CHECK(io.value(r) == doctest::Approx(55.0f));

    // ...and "force this to read as a dead sensor" is expressible too.
    f.bank.override_set(f.bank.var(r), from_f32(T_F32, 55.0f), false);
    CHECK_FALSE(io.valid(r));

    f.bank.override_clear(f.bank.var(r));
    CHECK(io.value(r) == doctest::Approx(20.0f));
}

TEST_CASE("plc io: an invalid row reads as its declared default") {
    // So logic that does not care about quality still gets a sane number
    // instead of whatever was in the slot.
    Live f;
    Io io(f.bank);
    const VarRef r = f.ref("cli_temp_ext1");

    f.bank.var(r).invalidate(Q_STALE);
    CHECK_FALSE(io.valid(r));
    CHECK(io.value(r) == doctest::Approx(f.bank.var(r).meta()->def.f));
}

TEST_CASE("plc io: a write lands on the row and is owed to the field") {
    Live f;
    Io io(f.bank);

    // A relay coil: a ROLE_OUTPUT, so the write has to be carried outward.
    VarRef out = { VAR_NONE };
    const VarTables& t = Site::g_site.vars;
    for (uint16_t i = 0; i < t.count; ++i) {
        if (t.var[i].role == ROLE_OUTPUT && t.var[i].type == T_BOOL) {
            out = f.ref(t.var[i].name);
            break;
        }
    }
    REQUIRE(out.index != VAR_NONE);

    f.bank.var(out).clear_field_dirty();
    io.set(out, true);

    INFO("'" << std::string(f.bank.var(out).name()) << "'");
    CHECK(io.on(out));
    CHECK(f.bank.var(out).field_dirty());

    io.set(out, false);
    CHECK_FALSE(io.on(out));
}

TEST_CASE("plc io: a row remembers which program drives it") {
    Live f;
    Io one(f.bank, 3);
    const VarRef r = f.ref("cli_temp_ext1");

    CHECK(f.bank.writer_of(r) == WRITER_NONE);
    one.set(r, 1.0f);
    CHECK(f.bank.writer_of(r) == 3);

    // The same program writing again, every beat, is the ordinary case and
    // must stay silent.
    for (int i = 0; i < 10; ++i) one.set(r, 2.0f);
    CHECK(f.bank.writer_conflicts() == 0);
}

TEST_CASE("plc io: a second program on one row is counted, and the write stands") {
    // Two authors is a defect in the site's design: the row gets whichever ran
    // last in the beat, so it chatters at the scan rate while both insist.
    Live f;
    Io one(f.bank, 1), two(f.bank, 2);
    const VarRef r = f.ref("cli_temp_ext1");

    one.set(r, 10.0f);
    REQUIRE(f.bank.writer_conflicts() == 0);

    two.set(r, 20.0f);
    CHECK(f.bank.writer_conflicts() == 1);
    CHECK(f.bank.first_conflict().index == r.index);

    INFO("the write is not refused - that would strand the plant wherever the "
         "losing program had got to");
    CHECK(f.bank.var(r).value() == doctest::Approx(20.0f));

    // Both keep writing every beat, each taking the row from the other. The
    // count is ROWS with two authors, so it settles at one instead of climbing
    // forever - which is what lets the driver report the defect once.
    for (int i = 0; i < 50; ++i) { one.set(r, 10.0f); two.set(r, 20.0f); }
    CHECK(f.bank.writer_conflicts() == 1);

    // A THIRD author is the same bad row, not a new one - but the row still
    // reports whoever wrote it last, rather than freezing on the first claimant.
    Io three(f.bank, 3);
    three.set(r, 30.0f);
    CHECK(f.bank.writer_conflicts() == 1);
    CHECK(f.bank.writer_of(r) == 3);

    // A DIFFERENT row with two authors is a second defect, and counts.
    const VarRef r2 = f.ref("cli_temp_ext2");
    one.set(r2, 1.0f);
    two.set(r2, 2.0f);
    CHECK(f.bank.writer_conflicts() == 2);
}

TEST_CASE("plc io: an unattributed view claims nothing") {
    // A program stepped by hand in a test has no slot. It must not claim rows
    // as writer 255 and then collide with a real program.
    Live f;
    Io anon(f.bank);
    const VarRef r = f.ref("cli_temp_ext1");

    anon.set(r, 1.0f);
    CHECK(f.bank.writer_of(r) == WRITER_NONE);
    CHECK(f.bank.writer_conflicts() == 0);

    Io real(f.bank, 0);
    real.set(r, 2.0f);
    CHECK(f.bank.writer_of(r) == 0);
    CHECK(f.bank.writer_conflicts() == 0);
}

TEST_CASE("scan: two programs on one row is named once, not every beat") {
    // The driver is the only thing that can turn a writer index back into a
    // program name, which is why the report lives there and the counting in
    // the bank.
    Live f;
    Logs::Logger logger;
    Criticals sink;
    logger.add_writer(&sink);

    Drives a("prog_a", "cli_temp_ext1");
    Drives b("prog_b", "cli_temp_ext1");
    Plc::Program* const progs[] = { &a, &b };
    Scan::Driver d(progs, 2, &f.bank, &logger);

    for (uint64_t t = 0; t <= 500; t += 10) d.step(t);

    INFO("last critical: " << std::string(sink.last));
    CHECK(f.bank.writer_conflicts() >= 1);
    INFO("a defect that is true on every beat must not be said on every beat");
    CHECK(sink.seen == 1);
    CHECK(std::string(sink.last).find("cli_temp_ext1") != std::string::npos);
    CHECK(std::string(sink.last).find("prog_") != std::string::npos);
}

TEST_CASE("scan: one program driving its own rows says nothing") {
    Live f;
    Logs::Logger logger;
    Criticals sink;
    logger.add_writer(&sink);

    Drives a("prog_a", "cli_temp_ext1");
    Drives b("prog_b", "cli_temp_ext2");
    Plc::Program* const progs[] = { &a, &b };
    Scan::Driver d(progs, 2, &f.bank, &logger);

    for (uint64_t t = 0; t <= 500; t += 10) d.step(t);

    CHECK(f.bank.writer_conflicts() == 0);
    CHECK(sink.seen == 0);
}

TEST_CASE("plc io: a bad reference moves nothing") {
    // A program driving a row that does not exist has to be inert, not land on
    // row zero - which with the parameters leading the table is a real setting.
    Live f;
    Io io(f.bank);
    const VarRef nowhere = f.ref("no_such_row_at_all");

    const Scalar first = f.bank.var(f.bank.find(Site::g_site.vars.var[0].name)).raw();
    io.set(nowhere, 1234.0f);
    const Scalar after = f.bank.var(f.bank.find(Site::g_site.vars.var[0].name)).raw();

    INFO("writing an unresolved reference must not touch the first row");
    CHECK(first.u == after.u);
}
