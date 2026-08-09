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
#include <vector>

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

/** @brief Appends its own name to a shared log when it runs, so a case can ask
 *  what ran in what order. */
class Ordered : public Plc::Program {
    std::vector<std::string>* _log;
public:
    Ordered(const char* name, std::vector<std::string>* log)
        : Program(name, 0), _log(log) {}
protected:
    void run(Plc::Context&) override { _log->push_back(name()); }
};

/** @brief Reads one row and writes another, so a pair of them can show whether
 *  the second saw what the first wrote IN THE SAME BEAT. */
class Relay : public Plc::Program {
    VarRef      _in = { VAR_NONE }, _out = { VAR_NONE };
    const char* _from;
    const char* _to;
    float       _add;
public:
    Relay(const char* name, const char* from, const char* to, float add)
        : Program(name, 0), _from(from), _to(to), _add(add) {}
protected:
    void bind(Bank& io) override { _in = io.find(_from); _out = io.find(_to); }
    void run(Plc::Context& c) override { c.io.set(_out, c.io.value(_in) + _add); }
};

/** @brief Records the lifecycle edges and what the first-scan flag said. */
class Lifecycle : public Plc::Program {
public:
    int  resumes = 0;
    int  resets  = 0;
    std::vector<bool> firsts;

    Lifecycle(const char* name, uint32_t period_ms) : Program(name, period_ms) {}

    void reset() override  { resets++; }
    void resume() override { resumes++; Program::resume(); }

protected:
    void run(Plc::Context& c) override { firsts.push_back(c.first_scan); }
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

TEST_CASE("scan: a program that keeps its period raises nothing") {
    Live f;
    Drives a("prog_a", "cli_temp_ext1");
    Plc::Program* const progs[] = { &a };
    Scan::Driver d(progs, 1, &f.bank);
    const VarRef late = f.ref("alarm_scan_late");

    for (uint64_t t = 0; t <= 1000; t += 10) d.step(t);

    CHECK(d.late_ms() == 0);
    CHECK(d.worst() == nullptr);
    CHECK_FALSE(f.bank.var(late).b());
    INFO("and it costs something measurable, per program rather than per scan");
    CHECK(a.cost().runs > 0);
}

TEST_CASE("scan: a program starved of beats is late, and named") {
    // Lateness is against the PROGRAM's own period, so one threshold serves
    // every rate. Here the driver is simply not stepped for a while, which is
    // what an overrunning beat looks like from a program's point of view.
    Live f;
    Drives slow("prog_slow", "cli_temp_ext1");
    slow.set_period(100);
    Plc::Program* const progs[] = { &slow };
    Scan::Driver d(progs, 1, &f.bank);
    const VarRef late = f.ref("alarm_scan_late");

    d.step(0);                       // seeds the timer
    CHECK(d.late_ms() == 0);

    d.step(1000);                    // 900 ms past its 100 ms period

    CHECK(d.late_ms() == 900);
    REQUIRE(d.worst() != nullptr);
    CHECK(std::string(d.worst()->name()) == "prog_slow");
    CHECK(slow.late_max_ms() == 900);
    INFO("the row is driven; the alarm's own dwell decides if it annunciates");
    CHECK(f.bank.var(late).b());

    // Back on schedule, and the row clears - the peak is kept, because that is
    // the number worth keeping.
    d.step(1100);
    CHECK(d.late_ms() == 0);
    CHECK_FALSE(f.bank.var(late).b());
    CHECK(slow.late_max_ms() == 900);
}

TEST_CASE("scan: an every-beat program is never reported late") {
    // Period 0 is due every beat and has no nominal to be late against, so it
    // must not raise the alarm just because the beat itself slipped.
    Live f;
    Drives every("prog_every", "cli_temp_ext1");
    Plc::Program* const progs[] = { &every };
    Scan::Driver d(progs, 1, &f.bank);

    d.step(0);
    d.step(5000);                    // an enormous gap

    CHECK(every.late_ms() == 0);
    CHECK(d.late_ms() == 0);
    CHECK_FALSE(f.bank.var(f.ref("alarm_scan_late")).b());
}

TEST_CASE("scan: the first activation says so, and only the first") {
    Live f;
    Lifecycle p("p", 0);
    Plc::Program* const progs[] = { &p };
    Scan::Driver d(progs, 1, &f.bank);

    for (uint64_t t = 0; t < 50; t += 10) d.step(t);

    REQUIRE(p.firsts.size() == 5);
    CHECK(p.firsts[0]);
    for (size_t i = 1; i < p.firsts.size(); ++i) {
        INFO("activation " << i);
        CHECK_FALSE(p.firsts[i]);
    }
}

TEST_CASE("scan: control coming back is a first scan again") {
    // The process moved without us, so the previous activation describes a
    // plant that no longer exists - which is the same situation bring-up is in.
    Live f;
    Lifecycle p("p", 0);
    Plc::Program* const progs[] = { &p };
    Scan::Driver d(progs, 1, &f.bank);

    d.step(0);
    d.step(10);
    REQUIRE(p.firsts.size() == 2);
    REQUIRE(p.firsts[0]);
    REQUIRE_FALSE(p.firsts[1]);

    f.bank.disable_control(Bank::INHIBIT_OPERATOR);
    for (uint64_t t = 20; t <= 5000; t += 10) d.step(t);
    CHECK(p.resets == 1);                    // the withdrawal edge, once
    CHECK(p.firsts.size() == 2);             // nothing ran while stopped

    f.bank.enable_control(Bank::INHIBIT_OPERATOR);
    d.step(5010);

    CHECK(p.resumes == 2);                   // bring-up, then this return
    REQUIRE(p.firsts.size() == 3);
    INFO("the run after control returns starts from nothing");
    CHECK(p.firsts[2]);

    d.step(5020);
    REQUIRE(p.firsts.size() == 4);
    CHECK_FALSE(p.firsts[3]);
}

TEST_CASE("scan: bring-up is itself a resume, and reset is not called for it") {
    // Control starts withheld and is granted before the first beat, so the
    // return edge fires at bring-up too. That is correct rather than incidental:
    // at bring-up a program knows no more about the plant than it does after an
    // outage, and a program that re-datums an actuator wants to do it in both.
    Live f;
    Lifecycle p("p", 0);
    Plc::Program* const progs[] = { &p };
    Scan::Driver d(progs, 1, &f.bank);

    d.step(0);

    CHECK(p.resumes == 1);
    INFO("nothing was ever driving, so nothing has gone stale");
    CHECK(p.resets == 0);
}

TEST_CASE("scan: table order is execution order") {
    // A site orders its programs on purpose - a law that computes a setpoint
    // before the law that acts on it - and nothing else in the firmware says so.
    // Not alphabetical, not by period, not by when they were constructed: the
    // order the site wrote them in.
    Live f;
    std::vector<std::string> ran;
    Ordered a("zulu",  &ran);
    Ordered b("alpha", &ran);
    Ordered c("mike",  &ran);
    Plc::Program* const progs[] = { &a, &b, &c };
    Scan::Driver d(progs, 3, &f.bank);

    d.step(0);

    REQUIRE(ran.size() == 3);
    INFO("declared zulu, alpha, mike - and that is the order, not sorted");
    CHECK(ran[0] == "zulu");
    CHECK(ran[1] == "alpha");
    CHECK(ran[2] == "mike");
}

TEST_CASE("scan: reordering the table reorders the beat") {
    // The same three programs, declared the other way round. If the order came
    // from anything but the table, this would not change.
    Live f;
    std::vector<std::string> ran;
    Ordered a("zulu",  &ran);
    Ordered b("alpha", &ran);
    Ordered c("mike",  &ran);
    Plc::Program* const progs[] = { &c, &b, &a };
    Scan::Driver d(progs, 3, &f.bank);

    d.step(0);

    REQUIRE(ran.size() == 3);
    CHECK(ran[0] == "mike");
    CHECK(ran[1] == "alpha");
    CHECK(ran[2] == "zulu");
}

TEST_CASE("scan: a later program sees what an earlier one wrote this beat") {
    // Which is WHY the order is a contract rather than an accident: the image
    // mutates between phases, not under a running program, so a chain within
    // one beat is exactly as long as the table says it is.
    Live f;
    const VarRef src = f.ref("cli_temp_ext1");
    const VarRef mid = f.ref("cli_temp_ext2");
    const VarRef dst = f.ref("cli_temp_ext3");
    f.bank.var(src).publish(from_f32(T_F32, 10.0f), 0);

    Relay first("first",  "cli_temp_ext1", "cli_temp_ext2", 1.0f);
    Relay second("second", "cli_temp_ext2", "cli_temp_ext3", 1.0f);

    SUBCASE("in order, the whole chain completes in one beat") {
        Plc::Program* const progs[] = { &first, &second };
        Scan::Driver d(progs, 2, &f.bank);
        d.step(0);

        CHECK(f.bank.var(mid).value() == doctest::Approx(11.0f));
        INFO("second read what first had just written");
        CHECK(f.bank.var(dst).value() == doctest::Approx(12.0f));
    }

    SUBCASE("out of order, the chain takes a beat per link") {
        Plc::Program* const progs[] = { &second, &first };
        Scan::Driver d(progs, 2, &f.bank);
        d.step(0);

        // second ran first, on the value mid held BEFORE first wrote it.
        CHECK(f.bank.var(mid).value() == doctest::Approx(11.0f));
        INFO("the far end is a beat behind, which is what mis-ordering costs");
        CHECK(f.bank.var(dst).value() != doctest::Approx(12.0f));

        d.step(10);
        CHECK(f.bank.var(dst).value() == doctest::Approx(12.0f));
    }
}

TEST_CASE("scan: a computed row can be traced back to the program that wrote it") {
    // What the MONITOR page's Mapping field turns into "computed (prog_a)".
    // The page only formats; this is the path it formats - writer_of() gives the
    // index, the driver turns it into a name, and neither is much use alone.
    Live f;
    Drives a("prog_a", "cli_temp_ext1");
    Drives b("prog_b", "cli_temp_ext2");
    Plc::Program* const progs[] = { &a, &b };
    Scan::Driver d(progs, 2, &f.bank);

    d.step(0);

    const uint8_t w1 = f.bank.writer_of(f.ref("cli_temp_ext1"));
    const uint8_t w2 = f.bank.writer_of(f.ref("cli_temp_ext2"));
    REQUIRE(d.program(w1) != nullptr);
    REQUIRE(d.program(w2) != nullptr);
    CHECK(std::string(d.program(w1)->name()) == "prog_a");
    CHECK(std::string(d.program(w2)->name()) == "prog_b");

    // A row no program has written names nobody, rather than naming program 0.
    const uint8_t none = f.bank.writer_of(f.ref("cli_temp_ext3"));
    CHECK(none == WRITER_NONE);
    CHECK(d.program(none) == nullptr);
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
